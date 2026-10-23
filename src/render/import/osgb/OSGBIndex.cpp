#include "OSGBIndex.h"
#include "OSGBFiles.h"
#include <cpl_minixml.h>
#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace geostudio::native::io::detail
{
namespace fs = std::filesystem;
namespace
{
using Json = nlohmann::json;
constexpr size_t maxRoots = 65536;
constexpr uint64_t cacheReadLimit = 32 * 1024 * 1024;
constexpr const char* cacheKind = "geostudio-osgb-index";

// 注册只发生一次；各读取调用独占 GDAL 数据集、游标和空间参考。
void initializeGdal()
{
    static std::once_flag once;
    std::call_once(once, [] { GDALAllRegister(); });
}
std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}
bool validDataset(const fs::path& directory)
{
    return fs::is_directory(directory) && fs::is_regular_file(directory / "metadata.xml") &&
        (fs::is_directory(directory / "Data") || fs::is_directory(directory / "data"));
}
fs::path dataDirectory(const fs::path& root)
{
    return fs::is_directory(root / "Data") ? root / "Data" : root / "data";
}
fs::path discoverRoot(const fs::path& input, const IndexReadOptions& options)
{
    if (!options.sourceRootOverride.empty())
    {
        if (!validDataset(options.sourceRootOverride)) throw std::runtime_error("OSGB sourceRoot override lacks metadata.xml or Data");
        return fs::canonical(options.sourceRootOverride);
    }
    if (fs::is_directory(input) && validDataset(input)) return fs::canonical(input);
    if (input.filename() == "metadata.xml" && validDataset(input.parent_path())) return fs::canonical(input.parent_path());
    if (lower(input.extension().u8string()) != ".shp") throw std::runtime_error("OSGB dataset input must be a directory, metadata.xml or SHP");
    // 仅在索引旁和两级父目录寻找约定的数据集，绝不跟随 DBF 中任意绝对路径。
    std::set<fs::path> candidates;
    auto parent = fs::absolute(input).parent_path();
    for (unsigned depth = 0; depth < 3 && !parent.empty(); ++depth)
    {
        for (const auto& candidate : {parent, parent / "OSGB"})
            if (validDataset(candidate)) candidates.insert(fs::canonical(candidate));
        const auto next = parent.parent_path();
        if (next == parent) break;
        parent = next;
    }
    if (candidates.size() != 1) throw std::runtime_error("SHP requires one nearby OSGB dataset or an explicit sourceRoot override");
    return *candidates.begin();
}
FileIdentity identity(const fs::path& path)
{
    if (!fs::is_regular_file(path)) throw std::runtime_error("Missing OSGB root: " + path.u8string());
    FileIdentity result;
    result.size = fs::file_size(path);
    result.modifiedTicks = static_cast<int64_t>(fs::last_write_time(path).time_since_epoch().count());
    return result;
}
fs::path resolveRoot(const DatasetIndex& index, const std::string& relative)
{
    const auto path = fs::u8path(relative);
    if (relative.empty() || path.is_absolute() || path.has_root_name() || relative.find(':') != std::string::npos)
        throw std::runtime_error("OSGB root path must be relative");
    for (const auto& component : path)
        if (component == ".." || component == ".") throw std::runtime_error("OSGB root path contains traversal");
    const auto result = index.sourceRoot / path;
    if (!pathWithin(result, index.sourceRoot) || lower(path.extension().u8string()) != ".osgb")
        throw std::runtime_error("OSGB root path escapes dataset or has an invalid extension");
    return result;
}
bool finiteSphere(const std::array<double, 4>& sphere)
{
    return sphere[3] > 0 && std::all_of(sphere.begin(), sphere.end(), [](double v) { return std::isfinite(v); });
}
template<size_t N> bool sameCoordinates(const std::array<double, N>& a, const std::array<double, N>& b)
{
    for (size_t i = 0; i < N; ++i)
        if (!std::isfinite(a[i]) || !std::isfinite(b[i]) || std::abs(a[i] - b[i]) > 1e-5) return false;
    return true;
}
void readMetadata(DatasetIndex& index)
{
    const auto path = index.sourceRoot / "metadata.xml";
    if (!pathWithin(path, index.sourceRoot)) throw std::runtime_error("OSGB metadata escapes dataset");
    index.metadataContents = readBoundedFile(path, 1024 * 1024);
    std::unique_ptr<CPLXMLNode, decltype(&CPLDestroyXMLNode)> xml(CPLParseXMLString(index.metadataContents.c_str()), CPLDestroyXMLNode);
    auto root = xml ? CPLGetXMLNode(xml.get(), "=ModelMetadata") : nullptr;
    if (!root) throw std::runtime_error("OSGB index requires ModelMetadata");
    index.srs = CPLGetXMLValue(root, "SRS", "");
    auto text = std::string(CPLGetXMLValue(root, "SRSOrigin", ""));
    std::replace(text.begin(), text.end(), ',', ' ');
    std::istringstream stream(text);
    std::string extra;
    if (!(stream >> index.origin[0] >> index.origin[1] >> index.origin[2]) || stream >> extra ||
        !std::all_of(index.origin.begin(), index.origin.end(), [](double v) { return std::isfinite(v); }))
        throw std::runtime_error("OSGB index requires three finite metadata origin coordinates");
    OGRSpatialReference source, target;
    if (source.SetFromUserInput(index.srs.c_str()) != OGRERR_NONE || !source.IsProjected() ||
        std::abs(source.GetLinearUnits() - 1.0) > 1e-12 || target.importFromEPSG(4978) != OGRERR_NONE)
        throw std::runtime_error("OSGB index requires a projected CRS with metre units");
    source.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    target.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    std::unique_ptr<OGRCoordinateTransformation, decltype(&OGRCoordinateTransformation::DestroyCT)> transform(
        OGRCreateCoordinateTransformation(&source, &target), OGRCoordinateTransformation::DestroyCT);
    auto anchor = index.origin;
    if (!transform || !transform->Transform(1, &anchor[0], &anchor[1], &anchor[2]) ||
        !std::all_of(anchor.begin(), anchor.end(), [](double v) { return std::isfinite(v); }))
        throw std::runtime_error("Cannot transform OSGB index origin to ECEF");
    index.ecefAnchor = anchor;
}
void addRoot(DatasetIndex& index, const fs::path& absolute, std::optional<PlanarBounds> footprint = {})
{
    if (index.roots.size() >= maxRoots) throw std::runtime_error("OSGB index root limit exceeded");
    RootRecord root;
    root.relativePath = absolute.lexically_relative(index.sourceRoot).generic_u8string();
    root.source = identity(resolveRoot(index, root.relativePath));
    root.footprint = footprint;
    index.roots.push_back(std::move(root));
}
void scanDirectory(DatasetIndex& index)
{
    const auto directory = dataDirectory(index.sourceRoot);
    if (!pathWithin(directory, index.sourceRoot)) throw std::runtime_error("OSGB Data directory escapes dataset");
    for (const auto& item : fs::directory_iterator(directory))
    {
        if (!item.is_directory()) continue;
        addRoot(index, item.path() / fs::u8path(item.path().filename().u8string() + ".osgb"));
    }
}
void readShapefile(DatasetIndex& index, const fs::path& input, bool relocated)
{
    const char* allowed[] = {"ESRI Shapefile", nullptr};
    // 固定 Windows GDAL 的 recode 后端识别 CP936，不识别 GBK/GB2312 别名。
    // 只覆盖当前只读数据集的打开选项，不修改 CPG 原件或进程全局编码设置。
    auto cpg = input;
    cpg.replace_extension(".cpg");
    std::string encoding;
    if (fs::is_regular_file(cpg))
    {
        auto label = lower(readBoundedFile(cpg, 256));
        if (label.size() >= 3 && label.compare(0, 3, "\xef\xbb\xbf") == 0) label.erase(0, 3);
        label.erase(std::remove_if(label.begin(), label.end(), [](unsigned char c) { return std::isspace(c) != 0; }), label.end());
        if (label == "gbk" || label == "gb2312" || label == "936") encoding = "ENCODING=CP936";
    }
    const char* openOptions[] = {encoding.empty() ? nullptr : encoding.c_str(), nullptr};
    std::unique_ptr<GDALDataset, decltype(&GDALClose)> dataset(static_cast<GDALDataset*>(
        GDALOpenEx(input.u8string().c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, allowed, openOptions, nullptr)), GDALClose);
    if (!dataset || dataset->GetLayerCount() != 1) throw std::runtime_error("Cannot open one-layer OSGB SHP index");
    auto layer = dataset->GetLayer(0);
    OGRSpatialReference source;
    if (source.SetFromUserInput(index.srs.c_str()) != OGRERR_NONE) throw std::runtime_error("Invalid OSGB metadata SRS");
    source.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    auto layerSrs = layer->GetSpatialRef();
    if (!layerSrs) throw std::runtime_error("OSGB SHP index has no spatial reference");
    std::unique_ptr<OGRSpatialReference> layerCopy(layerSrs->Clone());
    layerCopy->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    if (!source.IsSame(layerCopy.get())) throw std::runtime_error("OSGB SHP geometry SRS differs from metadata");
    const std::array<const char*, 6> names{{"SJY", "GWMC", "EPSG", "YDZBX", "YDZBY", "YDZBZ"}};
    std::array<int, 6> fields{};
    for (size_t i = 0; i < names.size(); ++i)
    {
        fields[i] = layer->GetLayerDefn()->GetFieldIndex(names[i]);
        if (fields[i] < 0) throw std::runtime_error(std::string("OSGB SHP missing field: ") + names[i]);
    }
    layer->ResetReading();
    while (auto raw = layer->GetNextFeature())
    {
        std::unique_ptr<OGRFeature, decltype(&OGRFeature::DestroyFeature)> feature(raw, OGRFeature::DestroyFeature);
        for (int field : fields) if (!feature->IsFieldSetAndNotNull(field)) throw std::runtime_error("OSGB SHP required field is null");
        const std::string name = feature->GetFieldAsString(fields[1]);
        const auto namePath = fs::u8path(name);
        if (name.empty() || namePath.has_parent_path() || namePath.has_root_path() || name == "." || name == ".." ||
            name.find_first_of("/\\:") != std::string::npos)
            throw std::runtime_error("OSGB SHP root name is not a safe basename");
        std::string epsg = feature->GetFieldAsString(fields[2]);
        if (epsg.find(':') == std::string::npos) epsg = "EPSG:" + epsg;
        OGRSpatialReference rowSrs;
        if (rowSrs.SetFromUserInput(epsg.c_str()) != OGRERR_NONE) throw std::runtime_error("Invalid OSGB SHP EPSG field");
        rowSrs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        if (!source.IsSame(&rowSrs)) throw std::runtime_error("OSGB SHP row SRS differs from metadata");
        std::array<double, 3> origin{{feature->GetFieldAsDouble(fields[3]), feature->GetFieldAsDouble(fields[4]), feature->GetFieldAsDouble(fields[5])}};
        if (!sameCoordinates(origin, index.origin)) throw std::runtime_error("OSGB SHP row origin differs from metadata");
        const auto expected = dataDirectory(index.sourceRoot) / namePath;
        std::string sourceDirectory = feature->GetFieldAsString(fields[0]);
        std::replace(sourceDirectory.begin(), sourceDirectory.end(), '\\', '/');
        auto advertised = fs::u8path(sourceDirectory).lexically_normal();
        if (advertised.filename().empty()) advertised = advertised.parent_path();
        if (advertised.filename().u8string() != name) throw std::runtime_error("OSGB SHP directory and root name disagree");
        // 旧绝对路径只用于字面核验；重定位时使用调用者指定根目录与约定相对布局。
        if (!relocated && advertised.is_absolute() && lower(advertised.generic_u8string()) != lower(expected.lexically_normal().generic_u8string()))
            throw std::runtime_error("OSGB SHP path differs from discovered dataset; provide sourceRoot override for relocation");
        auto geometry = feature->GetGeometryRef();
        if (!geometry || geometry->IsEmpty() ||
            (wkbFlatten(geometry->getGeometryType()) != wkbPolygon && wkbFlatten(geometry->getGeometryType()) != wkbMultiPolygon))
            throw std::runtime_error("OSGB SHP footprint must be a nonempty polygon");
        OGREnvelope envelope;
        geometry->getEnvelope(&envelope);
        PlanarBounds footprint{{envelope.MinX, envelope.MinY, envelope.MaxX, envelope.MaxY}};
        if (!std::all_of(footprint.xy.begin(), footprint.xy.end(), [](double v) { return std::isfinite(v); }) ||
            envelope.MinX >= envelope.MaxX || envelope.MinY >= envelope.MaxY)
            throw std::runtime_error("OSGB SHP footprint is invalid");
        addRoot(index, expected / fs::u8path(name + ".osgb"), footprint);
    }
}
bool isHash(const std::string& value)
{
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
// 缓存只是可重建加速项；损坏或不匹配时不影响原始目录/索引读取。
void restoreBounds(DatasetIndex& index, const fs::path& directory)
{
    if (directory.empty() || !fs::exists(directory / "index.json")) return;
    validateArtifactDirectory(directory, index.sourceRoot);
    try
    {
        const auto cached = Json::parse(readBoundedFile(directory / "index.json", cacheReadLimit));
        if (cached.at("kind") != cacheKind || cached.at("version") != 1 || cached.at("metadata") != index.metadataContents ||
            cached.at("coordinatePolicy") != index.coordinatePolicy || cached.at("srs") != index.srs ||
            !sameCoordinates(cached.at("origin").get<std::array<double, 3>>(), index.origin) || !index.ecefAnchor ||
            !sameCoordinates(cached.at("ecefAnchor").get<std::array<double, 3>>(), *index.ecefAnchor)) return;
        std::unordered_map<std::string, const Json*> byPath;
        for (const auto& record : cached.at("roots"))
        {
            auto path = record.at("relativePath").get<std::string>();
            if (!byPath.emplace(path, &record).second) return;
        }
        for (auto& root : index.roots)
        {
            auto found = byPath.find(root.relativePath);
            if (found == byPath.end()) continue;
            const auto& record = *found->second;
            if (!record.contains("localEcefSphere") || record.at("size").get<uint64_t>() != root.source.size ||
                record.at("modifiedTicks").get<int64_t>() != root.source.modifiedTicks) continue;
            const auto hash = record.at("sha256").get<std::string>();
            const auto sphere = record.at("localEcefSphere").get<std::array<double, 4>>();
            if (!isHash(hash) || !finiteSphere(sphere)) continue;
            const auto path = resolveRoot(index, root.relativePath);
            const auto actual = sha256File(path);
            const auto after = identity(path);
            if (actual != hash || after.size != root.source.size || after.modifiedTicks != root.source.modifiedTicks) continue;
            root.source.sha256 = actual;
            root.localEcefSphere = sphere;
        }
    }
    catch (const std::exception&)
    {
        // 任一结构损坏都丢弃本次恢复的球，避免混合不完整的缓存状态。
        for (auto& root : index.roots) { root.localEcefSphere.reset(); root.source.sha256.clear(); }
    }
}
}

DatasetIndex readDatasetIndex(const fs::path& input, const IndexReadOptions& options)
{
    initializeGdal();
    DatasetIndex index;
    index.sourceRoot = discoverRoot(input, options);
    index.coordinatePolicy = options.coordinatePolicy;
    if (index.coordinatePolicy.empty()) throw std::runtime_error("OSGB coordinate policy must be explicit");
    readMetadata(index);
    if (lower(input.extension().u8string()) == ".shp") readShapefile(index, input, !options.sourceRootOverride.empty());
    else scanDirectory(index);
    if (index.roots.empty()) throw std::runtime_error("OSGB dataset index contains no roots");
    std::sort(index.roots.begin(), index.roots.end(), [](const auto& a, const auto& b) { return a.relativePath < b.relativePath; });
    std::set<std::string> unique;
    for (const auto& root : index.roots)
        if (!unique.insert(lower(root.relativePath)).second) throw std::runtime_error("OSGB index contains duplicate roots");
    restoreBounds(index, options.cacheDirectory);
    return index;
}

void publishDatasetIndex(const DatasetIndex& index, const fs::path& cacheDirectory)
{
    validateArtifactDirectory(cacheDirectory, index.sourceRoot);
    if (index.roots.empty() || index.roots.size() > maxRoots || !index.ecefAnchor || index.coordinatePolicy.empty())
        throw std::runtime_error("OSGB index publication requires complete dataset context");
    if (readBoundedFile(index.sourceRoot / "metadata.xml", 1024 * 1024) != index.metadataContents)
        throw std::runtime_error("OSGB metadata changed before index publication");
    if (fs::exists(cacheDirectory / "index.json"))
    {
        // 不覆盖同名人工文件或不认识的旧格式；所有权由明确的类型标记限定。
        const auto previous = Json::parse(readBoundedFile(cacheDirectory / "index.json", cacheReadLimit));
        if (previous.value("kind", "") != cacheKind || previous.value("version", 0) != 1)
            throw std::runtime_error("OSGB index output already contains an unmanaged file");
    }
    Json result{{"kind", cacheKind}, {"version", 1}, {"metadata", index.metadataContents}, {"srs", index.srs},
        {"origin", index.origin}, {"ecefAnchor", *index.ecefAnchor}, {"coordinatePolicy", index.coordinatePolicy}, {"roots", Json::array()}};
    std::set<std::string> unique;
    for (const auto& root : index.roots)
    {
        if (!unique.insert(lower(root.relativePath)).second) throw std::runtime_error("OSGB publication contains duplicate roots");
        const auto path = resolveRoot(index, root.relativePath);
        const auto current = identity(path);
        if (current.size != root.source.size || current.modifiedTicks != root.source.modifiedTicks)
            throw std::runtime_error("OSGB root changed before index publication");
        Json value{{"relativePath", root.relativePath}, {"size", root.source.size}, {"modifiedTicks", root.source.modifiedTicks}};
        if (root.footprint) value["footprintXY"] = root.footprint->xy;
        if (root.localEcefSphere)
        {
            if (!finiteSphere(*root.localEcefSphere) || !isHash(root.source.sha256) || sha256File(path) != root.source.sha256)
                throw std::runtime_error("OSGB sphere requires verified decoded-source SHA-256");
            const auto after = identity(path);
            if (after.size != current.size || after.modifiedTicks != current.modifiedTicks)
                throw std::runtime_error("OSGB root changed during index publication");
            value["sha256"] = root.source.sha256;
            value["localEcefSphere"] = *root.localEcefSphere;
        }
        result["roots"].push_back(std::move(value));
    }
    const auto contents = result.dump(2);
    if (contents.size() > cacheReadLimit) throw std::runtime_error("OSGB index exceeds cache size limit");
    publishArtifactText(cacheDirectory, index.sourceRoot, "index.json", contents);
}
}
