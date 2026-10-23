#include "OSGBDataset.h"
#include "OSGBFiles.h"
#include "OSGBPaging.h"
#include "OSGBSerialization.h"
#include <vsg/io/VSG.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/VertexDraw.h>
#include <vsg/nodes/VertexIndexDraw.h>
#include <vsg/commands/Draw.h>
#include <vsg/commands/DrawIndexed.h>
#include <vsg/state/Image.h>
#include <vsg/state/ImageView.h>
#include <vsg/vk/ResourceRequirements.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace geostudio::native::io::detail
{
namespace fs = std::filesystem;
namespace
{
using Json = nlohmann::json;
constexpr uint64_t outputCeiling = 2ull * 1024 * 1024 * 1024;
constexpr uint64_t manifestLimit = 32ull * 1024 * 1024;
constexpr const char* sourcePathKey = "osgb.dataset.sourceRelative";
[[noreturn]] void fail(const std::string& message) { throw std::runtime_error("OSGB dataset: " + message); }
void checkCancelled(const DatasetCancelled& cancelled)
{ if (cancelled && cancelled()) fail("build cancelled; previous manifest preserved"); }
bool finite(const vsg::dsphere& sphere)
{
    return std::isfinite(sphere.x) && std::isfinite(sphere.y) && std::isfinite(sphere.z) &&
        std::isfinite(sphere.r) && sphere.r > 0.0;
}
Json sphereJson(const vsg::dsphere& value) { return {value.x, value.y, value.z, value.r}; }
vsg::dsphere sphereRead(const Json& json)
{
    if (!json.is_array() || json.size() != 4) fail("invalid sphere");
    vsg::dsphere result(json.at(0).get<double>(), json.at(1).get<double>(), json.at(2).get<double>(), json.at(3).get<double>());
    if (!finite(result)) fail("non-finite or empty sphere");
    return result;
}
vsg::dsphere unite(const std::vector<vsg::dsphere>& spheres)
{
    if (spheres.empty()) fail("cannot merge empty bounds");
    const auto limit = std::numeric_limits<double>::max();
    vsg::dvec3 minimum(limit, limit, limit), maximum(-limit, -limit, -limit);
    for (const auto& sphere : spheres)
    {
        if (!finite(sphere)) fail("root bound must be trusted and finite");
        for (unsigned axis = 0; axis != 3; ++axis)
        { minimum[axis] = std::min(minimum[axis], sphere[axis] - sphere.r); maximum[axis] = std::max(maximum[axis], sphere[axis] + sphere.r); }
    }
    const auto center = (minimum + maximum) * 0.5;
    double radius = 0.0;
    for (const auto& sphere : spheres)
        radius = std::max(radius, vsg::length(vsg::dvec3(sphere.x, sphere.y, sphere.z) - center) + sphere.r);
    const vsg::dsphere result(center, radius);
    if (!finite(result)) fail("source bounds union overflow");
    return result;
}
fs::path relativeFile(const fs::path& directory, const std::string& relative)
{
    const auto name = fs::u8path(relative);
    if (relative.empty() || name.is_absolute() || name.has_root_name() || relative.find(':') != std::string::npos)
        fail("expected a relative file path");
    for (const auto& component : name) if (component == "." || component == "..") fail("relative path contains traversal");
    const auto resolved = directory / name;
    if (!pathWithin(resolved, directory) || !fs::is_regular_file(resolved)) fail("missing or escaped file: " + relative);
    return resolved;
}
FileIdentity identity(const fs::path& path)
{
    FileIdentity result;
    result.size = fs::file_size(path);
    result.modifiedTicks = static_cast<int64_t>(fs::last_write_time(path).time_since_epoch().count());
    result.sha256 = sha256File(path);
    if (result.size != fs::file_size(path) || result.modifiedTicks != static_cast<int64_t>(fs::last_write_time(path).time_since_epoch().count()))
        fail("file changed while hashing: " + path.u8string());
    return result;
}
Json identityJson(const FileIdentity& value) { return {{"size", value.size}, {"sha256", value.sha256}}; }
FileIdentity identityRead(const Json& json)
{
    FileIdentity result;
    result.size = json.at("size").get<uint64_t>(); result.sha256 = json.at("sha256").get<std::string>();
    if (result.sha256.size() != 64 || !std::all_of(result.sha256.begin(), result.sha256.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); })) fail("invalid SHA-256");
    return result;
}
void verify(const fs::path& path, const FileIdentity& expected)
{
    if (!fs::is_regular_file(path) || fs::file_size(path) != expected.size || identity(path).sha256 != expected.sha256)
        fail("content identity mismatch: " + path.u8string());
}
Json regionJson(const DatasetRegion& region)
{
    Json sources = Json::array();
    for (const auto& source : region.sources)
        sources.push_back({{"path", source.relativePath}, {"identity", identityJson(source.identity)}});
    return {{"id", region.id}, {"entry", region.entry}, {"entryIdentity", identityJson(region.entryIdentity)},
        {"full", region.full}, {"fullIdentity", identityJson(region.fullIdentity)},
        {"overview", region.overview}, {"overviewIdentity", identityJson(region.overviewIdentity)},
        {"bound", sphereJson(region.bound)}, {"sources", sources}, {"childReferences", region.childReferences}};
}
DatasetRegion regionRead(const Json& json)
{
    DatasetRegion result;
    result.id = json.at("id").get<std::string>(); result.entry = json.at("entry").get<std::string>();
    result.full = json.at("full").get<std::string>(); result.overview = json.at("overview").get<std::string>();
    result.entryIdentity = identityRead(json.at("entryIdentity"));
    result.fullIdentity = identityRead(json.at("fullIdentity")); result.overviewIdentity = identityRead(json.at("overviewIdentity"));
    result.bound = sphereRead(json.at("bound"));
    if (json.at("sources").size() > 65536 || json.at("childReferences").size() > 262144) fail("region dependency count exceeds limit");
    for (const auto& source : json.at("sources"))
        result.sources.push_back({source.at("path").get<std::string>(), identityRead(source.at("identity"))});
    result.childReferences = json.at("childReferences").get<std::vector<std::string>>();
    if (result.sources.empty()) fail("region has no source identities");
    return result;
}
void validateSources(const DatasetManifest& manifest, const DatasetRegion& region)
{
    for (const auto& source : region.sources) verify(relativeFile(manifest.sourceRoot, source.relativePath), source.identity);
    for (const auto& child : region.childReferences) relativeFile(manifest.sourceRoot, child);
}
struct Count : vsg::CollectResourceRequirements
{
    std::set<const vsg::Data*> seen;
    uint64_t bytes = 0, draws = 0;
    void add(const vsg::Data* data) { if (data && seen.insert(data).second) bytes += data->dataSize(); }
    void apply(vsg::ref_ptr<vsg::BufferInfo> buffer, BufferProperties) override { if (buffer) add(buffer->data); }
    void apply(vsg::ref_ptr<vsg::ImageInfo> image) override
    { if (image && image->imageView && image->imageView->image) add(image->imageView->image->data); }
    void apply(const vsg::VertexDraw& draw) override
    { if (draw.vertexCount && draw.instanceCount) ++draws; vsg::CollectResourceRequirements::apply(draw); }
    void apply(const vsg::VertexIndexDraw& draw) override
    { if (draw.indexCount && draw.instanceCount) ++draws; vsg::CollectResourceRequirements::apply(draw); }
    void apply(const vsg::Draw& draw) override { if (draw.vertexCount && draw.instanceCount) ++draws; }
    void apply(const vsg::DrawIndexed& draw) override { if (draw.indexCount && draw.instanceCount) ++draws; }
};
// 建造者独占独立页；保存路径身份即可，Options/Reader/token 从不进入制品。
struct Paths : vsg::Visitor
{
    fs::path sourceRoot;
    vsg::ref_ptr<const vsg::Options> options;
    std::set<std::string> references;
    bool loading = false, overview = false;
    void apply(vsg::Node& node) override { node.traverse(*this); }
    void apply(vsg::Group& group) override { for (auto& child : group.children) if (child) child->accept(*this); }
    void apply(vsg::PagedLOD& page) override
    {
        if (overview) fail("overview must not contain paging nodes");
        if (dynamic_cast<OSGBRoot*>(&page) || page.children[0].node || page.pending || page.requestStatus != vsg::PagedLOD::NoRequest)
            fail("runtime or already attached page cannot be serialized as a full root");
        if (page.filename)
        {
            std::string relative;
            if (loading)
            {
                if (!page.getValue(sourcePathKey, relative) || relative != page.filename.string()) fail("page path identity missing or inconsistent");
            }
            else
            {
                auto file = fs::u8path(page.filename.string());
                if (!file.is_absolute()) file = sourceRoot / file;
                if (!pathWithin(file, sourceRoot) || !fs::is_regular_file(file)) fail("original child reference is missing or escaped");
                relative = fs::relative(fs::canonical(file), fs::canonical(sourceRoot)).generic_u8string();
                page.setValue(sourcePathKey, relative);
            }
            const auto source = relativeFile(sourceRoot, relative);
            references.insert(relative);
            page.filename = loading ? vsg::Path(source.wstring()) : vsg::Path(relative);
        }
        if (loading) page.options = options ? vsg::Options::create(*options) : vsg::Options::create();
        page.traverse(*this);
    }
};
vsg::ref_ptr<vsg::Node> readNode(const fs::path& path, const FileIdentity& expected,
    const fs::path& sourceRoot, vsg::ref_ptr<const vsg::Options> options, bool overview)
{
    verify(path, expected);
    // stream 读取避免 VSG 将子页错误地相对派生目录重定位；不经过当前自定义 Reader。
    std::ifstream input(path, std::ios::binary);
    auto reader = vsg::VSG::create();
    auto streamOptions = options ? vsg::Options::create(*options) : vsg::Options::create();
    streamOptions->extensionHint = ".vsgb";
    auto node = reader->read(input, streamOptions).cast<vsg::Node>();
    if (!node) fail("invalid VSG region page");
    verify(path, expected);
    Paths paths; paths.sourceRoot = sourceRoot; paths.options = options; paths.loading = true; paths.overview = overview;
    node->accept(paths);
    Count count; node->accept(count);
    if (!count.draws) fail("page contains no resident geometry");
    return node;
}
std::vector<size_t> mortonOrder(const DatasetIndex& index)
{
    const bool planar = std::all_of(index.roots.begin(), index.roots.end(), [](const auto& root) { return bool(root.footprint); });
    std::vector<std::array<double, 3>> centers;
    std::array<double, 3> minimum{INFINITY, INFINITY, INFINITY}, maximum{-INFINITY, -INFINITY, -INFINITY};
    for (const auto& root : index.roots)
    {
        std::array<double, 3> center{};
        if (planar) center = {(root.footprint->xy[0] + root.footprint->xy[2]) * 0.5, (root.footprint->xy[1] + root.footprint->xy[3]) * 0.5, 0.0};
        else
        {
            if (!root.localEcefSphere) fail("Morton grouping requires footprints or a prior trusted bounds pass");
            center = {(*root.localEcefSphere)[0], (*root.localEcefSphere)[1], (*root.localEcefSphere)[2]};
        }
        for (unsigned axis = 0; axis != 3; ++axis)
        {
            if (!std::isfinite(center[axis])) fail("invalid Morton coordinate");
            minimum[axis] = std::min(minimum[axis], center[axis]); maximum[axis] = std::max(maximum[axis], center[axis]);
        }
        centers.push_back(center);
    }
    std::vector<std::pair<uint64_t, size_t>> keys;
    for (size_t i = 0; i != centers.size(); ++i)
    {
        uint64_t key = 0;
        for (unsigned axis = 0; axis != 3; ++axis)
        {
            const double range = maximum[axis] - minimum[axis];
            if (!std::isfinite(range)) fail("Morton coordinate range overflow");
            const uint64_t value = range > 0.0 ? static_cast<uint64_t>(std::clamp((centers[i][axis] - minimum[axis]) / range, 0.0, 1.0) * 1023.0) : 0;
            for (unsigned bit = 0; bit != 10; ++bit) key |= ((value >> bit) & 1) << (bit * 3 + axis);
        }
        keys.emplace_back(key, i);
    }
    std::stable_sort(keys.begin(), keys.end(), [&](const auto& a, const auto& b) {
        return a.first != b.first ? a.first < b.first : index.roots[a.second].relativePath < index.roots[b.second].relativePath;
    });
    std::vector<size_t> result;
    for (const auto& key : keys) result.push_back(key.second);
    return result;
}
#ifdef _WIN32
struct Handle
{
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE h) : value(h) { if (value == INVALID_HANDLE_VALUE) fail("output is locked or pending file already exists"); }
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
class LimitedOutput : public std::streambuf
{
public:
    HANDLE handle;
    uint64_t limit, written = 0;
    DatasetCancelled cancelled;
    LimitedOutput(HANDLE h, uint64_t maximum, DatasetCancelled stop) : handle(h), limit(maximum), cancelled(std::move(stop)) {}
    std::streamsize xsputn(const char* data, std::streamsize length) override
    {
        if (length < 0 || uint64_t(length) > limit - written || (cancelled && cancelled())) return 0;
        std::streamsize completed = 0;
        while (completed < length)
        {
            if (cancelled && cancelled()) break;
            const DWORD size = static_cast<DWORD>(std::min<std::streamsize>(length - completed, 65536));
            DWORD actual = 0;
            if (!WriteFile(handle, data + completed, size, &actual, nullptr) || actual != size) break;
            completed += actual; written += actual;
        }
        return completed;
    }
    int_type overflow(int_type value) override
    {
        if (traits_type::eq_int_type(value, traits_type::eof())) return traits_type::not_eof(value);
        const char c = traits_type::to_char_type(value);
        return xsputn(&c, 1) == 1 ? value : traits_type::eof();
    }
};
#endif
uint64_t directoryBytes(const fs::path& directory)
{
    uint64_t result = 0;
    if (fs::exists(directory)) for (const auto& entry : fs::recursive_directory_iterator(directory))
    {
        if (entry.is_symlink()) fail("artifact directory contains a symbolic link");
        if (entry.is_regular_file())
        {
            const auto size = entry.file_size();
            if (size > outputCeiling - result) fail("existing output exceeds 2 GiB limit");
            result += size;
        }
    }
    return result;
}
std::pair<std::string, FileIdentity> publishNode(const fs::path& directory, const std::string& prefix,
    vsg::Node& node, uint64_t& outputBytes, uint64_t limit, uint64_t normalizationBytes, const DatasetCancelled& cancelled)
{
#ifdef _WIN32
    const auto pending = directory / fs::u8path(prefix + ".vsgb.pending");
    if (!pathWithin(pending, directory)) fail("pending path escapes artifact output");
    Handle file(CreateFileW(pending.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    LimitedOutput buffer(file.value, limit - outputBytes, cancelled);
    std::ostream stream(&buffer);
    auto options = vsg::Options::create(); options->extensionHint = ".vsgb";
    OSGBPlainImages plain(node, normalizationBytes);
    if (!vsg::VSG::create()->write(&node, stream, options) || !stream.good() || !FlushFileBuffers(file.value))
        fail("page write cancelled, exceeds output budget, or failed; pending preserved");
    if (!CloseHandle(file.value)) fail("cannot close pending artifact");
    file.value = INVALID_HANDLE_VALUE;
    const auto content = identity(pending);
    const auto filename = prefix + "-" + content.sha256 + ".vsgb";
    const auto target = directory / fs::u8path(filename);
    if (fs::exists(target))
    {
        verify(target, content);
        // 只移除本调用创建且已验证重复的一个 pending 文件，不清理旧代制品。
        if (!DeleteFileW(pending.c_str())) fail("cannot release duplicate pending artifact");
    }
    else
    {
        if (!MoveFileExW(pending.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH)) fail("cannot publish region page");
        outputBytes += content.size;
    }
    return {filename, content};
#else
    (void)directory; (void)prefix; (void)node; (void)outputBytes; (void)limit; (void)normalizationBytes; (void)cancelled;
    fail("atomic dataset output currently requires Windows");
#endif
}
void publishJson(const fs::path& directory, const fs::path& sourceRoot, const std::string& filename,
    const std::string& contents, uint64_t& outputBytes, uint64_t limit)
{
    const auto target = directory / fs::u8path(filename);
    if (fs::exists(target) && readBoundedFile(target, manifestLimit) == contents) return;
    const auto previousSize = fs::exists(target) ? fs::file_size(target) : 0;
    // 原子发布期间旧文件和 pending 同时存在，预算须覆盖二者。
    if (contents.size() > limit - outputBytes) fail("manifest exceeds output budget");
    publishArtifactText(directory, sourceRoot, filename, contents);
    outputBytes = outputBytes - previousSize + contents.size();
}
Json manifestJson(const DatasetManifest& manifest)
{
    Json regions = Json::array();
    for (const auto& region : manifest.regions) regions.push_back(regionJson(region));
    Json metadata{{"srs", manifest.metadata.srs}, {"origin", manifest.metadata.origin},
        {"coordinatePolicy", manifest.metadata.coordinatePolicy}, {"contents", manifest.metadata.metadataContents}};
    if (manifest.metadata.ecefAnchor) metadata["ecefAnchor"] = *manifest.metadata.ecefAnchor;
    return {{"kind", "geostudio-osgb-dataset"}, {"version", 1}, {"conversionIdentity", manifest.conversionIdentity},
        {"sourceRoot", manifest.sourceRoot.generic_u8string()}, {"metadata", metadata},
        {"overviewIdentity", manifest.overviewIdentity}, {"sourceIndexIdentity", manifest.sourceIndexIdentity}, {"rootsPerRegion", manifest.rootsPerRegion},
        {"metadataIdentity", identityJson(manifest.metadataIdentity)}, {"bound", sphereJson(manifest.bound)},
        {"regionScreenHeightRatio", manifest.regionScreenHeightRatio}, {"regions", regions},
        {"statistics", {{"roots", manifest.statistics.roots}, {"regions", manifest.statistics.regions},
            {"inputTriangles", manifest.statistics.inputTriangles}, {"outputTriangles", manifest.statistics.outputTriangles}}}};
}
}

DatasetManifest inspectManifest(const fs::path& path, const std::string& expectedConversionIdentity, const fs::path& sourceRootOverride)
{
    const auto json = Json::parse(readBoundedFile(path, manifestLimit));
    if (json.at("kind") != "geostudio-osgb-dataset" || json.at("version") != 1) fail("unsupported dataset manifest");
    DatasetManifest result;
    result.manifestPath = fs::canonical(path);
    result.conversionIdentity = json.at("conversionIdentity").get<std::string>();
    if (expectedConversionIdentity.empty() || result.conversionIdentity != expectedConversionIdentity)
        fail("converter version changed; rebuild this dataset or open the source metadata.xml");
    result.overviewIdentity = json.at("overviewIdentity").get<std::string>();
    result.sourceIndexIdentity = json.at("sourceIndexIdentity").get<std::string>();
    result.rootsPerRegion = json.at("rootsPerRegion").get<uint32_t>();
    if (!result.rootsPerRegion || result.rootsPerRegion > 16) fail("invalid roots per region");
    result.sourceRoot = fs::canonical(sourceRootOverride.empty() ? fs::u8path(json.at("sourceRoot").get<std::string>()) : sourceRootOverride);
    validateArtifactDirectory(result.manifestPath.parent_path(), result.sourceRoot);
    result.metadataIdentity = identityRead(json.at("metadataIdentity"));
    verify(relativeFile(result.sourceRoot, "metadata.xml"), result.metadataIdentity);
    const auto& metadata = json.at("metadata");
    result.metadata.sourceRoot = result.sourceRoot;
    result.metadata.srs = metadata.at("srs").get<std::string>(); result.metadata.origin = metadata.at("origin").get<std::array<double, 3>>();
    result.metadata.coordinatePolicy = metadata.at("coordinatePolicy").get<std::string>();
    result.metadata.metadataContents = metadata.at("contents").get<std::string>();
    if (sha256Bytes(result.metadata.metadataContents) != result.metadataIdentity.sha256) fail("metadata content identity mismatch");
    if (metadata.contains("ecefAnchor")) result.metadata.ecefAnchor = metadata.at("ecefAnchor").get<std::array<double, 3>>();
    result.bound = sphereRead(json.at("bound"));
    result.regionScreenHeightRatio = json.at("regionScreenHeightRatio").get<double>();
    if (!std::isfinite(result.regionScreenHeightRatio) || result.regionScreenHeightRatio <= 0.0) fail("invalid region threshold");
    if (!json.at("regions").is_array() || json.at("regions").empty() || json.at("regions").size() > 65536) fail("invalid region count");
    std::set<std::string> names;
    uint64_t payloadBytes = 0;
    for (const auto& value : json.at("regions"))
    {
        auto region = regionRead(value);
        for (const auto bytes : {region.fullIdentity.size, region.overviewIdentity.size})
        {
            if (bytes > outputCeiling - payloadBytes) fail("dataset payload exceeds 2 GiB limit");
            payloadBytes += bytes;
        }
        if (!names.insert(region.entry).second) fail("duplicate region entry");
        relativeFile(result.manifestPath.parent_path(), region.entry);
        relativeFile(result.manifestPath.parent_path(), region.full);
        relativeFile(result.manifestPath.parent_path(), region.overview);
        result.regions.push_back(std::move(region));
    }
    const auto& stats = json.at("statistics");
    result.statistics.manifestPath = result.manifestPath;
    result.statistics.roots = stats.at("roots").get<uint64_t>(); result.statistics.regions = stats.at("regions").get<uint64_t>();
    result.statistics.inputTriangles = stats.at("inputTriangles").get<uint64_t>(); result.statistics.outputTriangles = stats.at("outputTriangles").get<uint64_t>();
    return result;
}

DatasetBuildResult buildDataset(const DatasetIndex& index, const DatasetBuildSettings& settings,
    const PrepareDatasetRoot& prepare, const DatasetCancelled& cancelled)
{
    if (!prepare || index.roots.empty() || index.roots.size() > 65536 || !settings.rootsPerRegion || settings.rootsPerRegion > 16 ||
        !settings.maxWorkingBytes || !settings.maxOutputBytes || settings.maxOutputBytes > outputCeiling || settings.conversionIdentity.empty() ||
        !std::isfinite(settings.regionScreenHeightRatio) || settings.regionScreenHeightRatio <= 0.0) fail("invalid build settings");
    validateArtifactDirectory(settings.outputDirectory, index.sourceRoot);
    const auto order = mortonOrder(index);
    Json indexEntries = Json::array();
    for (auto position : order)
    {
        const auto& root = index.roots[position];
        Json value{{"path", root.relativePath}};
        if (root.footprint) value["footprint"] = root.footprint->xy;
        if (root.localEcefSphere) value["sphere"] = *root.localEcefSphere;
        indexEntries.push_back(std::move(value));
    }
    Json identityDescription{{"roots", indexEntries}, {"srs", index.srs}, {"origin", index.origin}, {"coordinatePolicy", index.coordinatePolicy}};
    if (index.ecefAnchor) identityDescription["ecefAnchor"] = *index.ecefAnchor;
    const auto sourceIndexIdentity = sha256Bytes(identityDescription.dump());
    fs::create_directories(settings.outputDirectory);
    validateArtifactDirectory(settings.outputDirectory, index.sourceRoot);
    uint64_t outputBytes = directoryBytes(settings.outputDirectory);
    if (outputBytes > settings.maxOutputBytes) fail("existing output exceeds configured budget");
#ifdef _WIN32
    const auto lockPath = settings.outputDirectory / "dataset-build.lock";
    Handle lock(CreateFileW(lockPath.c_str(), GENERIC_WRITE | DELETE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, nullptr));
#else
    fail("dataset build currently requires Windows");
#endif
    checkCancelled(cancelled);
    const auto manifestPath = settings.outputDirectory / "dataset.osgbset";
    if (fs::exists(manifestPath))
    {
        // 未变化的数据集直接复用；验证失败保留旧文件，再发布新的内容寻址区域。
        try
        {
            auto previous = inspectManifest(manifestPath, settings.conversionIdentity, index.sourceRoot);
            if (previous.statistics.roots == index.roots.size() && previous.regionScreenHeightRatio == settings.regionScreenHeightRatio &&
                previous.overviewIdentity == settings.overviewIdentity && previous.rootsPerRegion == settings.rootsPerRegion &&
                previous.sourceIndexIdentity == sourceIndexIdentity &&
                previous.regions.size() == (index.roots.size() + settings.rootsPerRegion - 1) / settings.rootsPerRegion)
            {
                std::set<std::string> previousSources;
                for (const auto& region : previous.regions)
                {
                    validateSources(previous, region);
                    verify(relativeFile(settings.outputDirectory, region.entry), region.entryIdentity);
                    verify(relativeFile(settings.outputDirectory, region.full), region.fullIdentity);
                    verify(relativeFile(settings.outputDirectory, region.overview), region.overviewIdentity);
                    for (const auto& source : region.sources) previousSources.insert(source.relativePath);
                }
                if (std::all_of(index.roots.begin(), index.roots.end(), [&](const auto& root) { return previousSources.count(root.relativePath) != 0; }))
                {
                    auto result = previous.statistics; result.outputBytes = outputBytes; result.reused = true;
                    checkCancelled(cancelled); return result;
                }
            }
        }
        catch (const std::exception&) { /* 无效旧制品不用于加载，最终入口成功后才替换。 */ }
        const auto old = Json::parse(readBoundedFile(manifestPath, manifestLimit));
        if (old.value("kind", "") != "geostudio-osgb-dataset") fail("refuse to replace unknown dataset entry");
    }
    DatasetManifest manifest;
    manifest.manifestPath = fs::absolute(manifestPath); manifest.sourceRoot = fs::canonical(index.sourceRoot); manifest.metadata = index;
    manifest.metadataIdentity = identity(relativeFile(index.sourceRoot, "metadata.xml"));
    if (manifest.metadataIdentity.sha256 != sha256Bytes(index.metadataContents)) fail("metadata changed since indexing");
    manifest.conversionIdentity = settings.conversionIdentity; manifest.regionScreenHeightRatio = settings.regionScreenHeightRatio;
    manifest.overviewIdentity = settings.overviewIdentity; manifest.rootsPerRegion = settings.rootsPerRegion; manifest.sourceIndexIdentity = sourceIndexIdentity;
    manifest.statistics.manifestPath = manifest.manifestPath; manifest.statistics.roots = index.roots.size();
    std::vector<vsg::dsphere> regionBounds;
    for (size_t offset = 0; offset < order.size(); offset += settings.rootsPerRegion)
    {
        checkCancelled(cancelled);
        auto full = vsg::Group::create(), overview = vsg::Group::create();
        DatasetRegion region; region.id = "region-" + std::to_string(offset / settings.rootsPerRegion);
        std::vector<vsg::dsphere> sourceBounds;
        std::map<std::string, FileIdentity> dependencies;
        std::set<std::string> childReferences;
        uint64_t workingBytes = 0;
        for (size_t position = offset; position < std::min(order.size(), offset + settings.rootsPerRegion); ++position)
        {
            checkCancelled(cancelled);
            const auto& root = index.roots[order[position]];
            const auto source = relativeFile(index.sourceRoot, root.relativePath);
            const auto before = identity(source);
            auto prepared = prepare(root, settings.maxWorkingBytes - workingBytes);
            checkCancelled(cancelled);
            verify(source, before);
            if (!root.source.sha256.empty() && root.source.sha256 != before.sha256) fail("root changed since bounds indexing");
            if (!prepared.full || !prepared.overview || !finite(prepared.bound)) fail("root callback returned incomplete content or bounds");
            Count count; prepared.full->accept(count); const auto fullDraws = count.draws;
            prepared.overview->accept(count);
            if (!fullDraws || count.draws == fullDraws) fail("full and overview must both contain resident geometry");
            const uint64_t required = std::max(prepared.workingBytes, count.bytes);
            if (required > settings.maxWorkingBytes - workingBytes) fail("region working payload exceeds configured budget");
            workingBytes += required;
            Paths fullPaths; fullPaths.sourceRoot = index.sourceRoot; prepared.full->accept(fullPaths);
            Paths overviewPaths; overviewPaths.sourceRoot = index.sourceRoot; overviewPaths.overview = true; prepared.overview->accept(overviewPaths);
            childReferences.insert(fullPaths.references.begin(), fullPaths.references.end());
            dependencies[root.relativePath] = before;
            for (const auto& dependency : prepared.sourceDependencies)
            {
                if (dependency.identity.sha256.empty()) fail("callback dependency lacks read-time SHA-256");
                verify(relativeFile(index.sourceRoot, dependency.relativePath), dependency.identity);
                const auto previous = dependencies.find(dependency.relativePath);
                if (previous != dependencies.end() && previous->second.sha256 != dependency.identity.sha256) fail("dependency changed between roots");
                dependencies[dependency.relativePath] = dependency.identity;
            }
            full->addChild(prepared.full); overview->addChild(prepared.overview); sourceBounds.push_back(prepared.bound);
            manifest.statistics.inputTriangles += prepared.inputTriangles; manifest.statistics.outputTriangles += prepared.outputTriangles;
        }
        region.bound = unite(sourceBounds); regionBounds.push_back(region.bound);
        for (const auto& [path, file] : dependencies) region.sources.push_back({path, file});
        region.childReferences.assign(childReferences.begin(), childReferences.end());
        const auto fullFile = publishNode(settings.outputDirectory, region.id + "-full", *full, outputBytes, settings.maxOutputBytes, settings.maxWorkingBytes - workingBytes, cancelled);
        region.full = fullFile.first; region.fullIdentity = fullFile.second;
        const auto overviewFile = publishNode(settings.outputDirectory, region.id + "-overview", *overview, outputBytes, settings.maxOutputBytes, settings.maxWorkingBytes - workingBytes, cancelled);
        region.overview = overviewFile.first; region.overviewIdentity = overviewFile.second;
        const std::string entry = Json{{"kind", "geostudio-osgb-region"}, {"version", 1}, {"manifest", "dataset.osgbset"}, {"id", region.id},
            {"full", region.full}, {"conversionIdentity", settings.conversionIdentity}}.dump();
        region.entry = region.id + "-" + sha256Bytes(entry) + ".osgbregion";
        publishJson(settings.outputDirectory, index.sourceRoot, region.entry, entry, outputBytes, settings.maxOutputBytes);
        region.entryIdentity = identity(settings.outputDirectory / fs::u8path(region.entry));
        manifest.regions.push_back(std::move(region));
    }
    manifest.bound = unite(regionBounds); manifest.statistics.regions = manifest.regions.size();
    // 提交入口前再次核对已消费的所有源，避免跨区域构建期间源数据变化。
    verify(relativeFile(index.sourceRoot, "metadata.xml"), manifest.metadataIdentity);
    for (const auto& region : manifest.regions) { checkCancelled(cancelled); validateSources(manifest, region); }
    const auto contents = manifestJson(manifest).dump(2);
    checkCancelled(cancelled);
    publishJson(settings.outputDirectory, index.sourceRoot, "dataset.osgbset", contents, outputBytes, settings.maxOutputBytes);
    manifest.statistics.outputBytes = outputBytes;
    return manifest.statistics;
}

vsg::ref_ptr<vsg::Node> loadOverviewGroup(const DatasetManifest& manifest, vsg::ref_ptr<const vsg::Options> options)
{
    verify(relativeFile(manifest.sourceRoot, "metadata.xml"), manifest.metadataIdentity);
    auto group = vsg::Group::create();
    for (const auto& region : manifest.regions)
    {
        const auto* token = options ? options->getObject<OSGBPagingToken>(osgbPagingTokenKey) : nullptr;
        if (token && token->closed()) fail("layer closed during overview load");
        validateSources(manifest, region);
        const auto entry = relativeFile(manifest.manifestPath.parent_path(), region.entry); verify(entry, region.entryIdentity);
        auto overview = readNode(relativeFile(manifest.manifestPath.parent_path(), region.overview), region.overviewIdentity, manifest.sourceRoot, options, true);
        auto page = vsg::PagedLOD::create();
        page->bound = region.bound; page->filename = vsg::Path(entry.wstring());
        page->options = options ? vsg::Options::create(*options) : vsg::Options::create();
        page->children[0].minimumScreenHeightRatio = manifest.regionScreenHeightRatio;
        page->children[1].minimumScreenHeightRatio = 0.0; page->children[1].node = overview;
        group->addChild(page);
    }
    return group;
}
vsg::ref_ptr<vsg::Node> readRegion(const fs::path& path, const std::string& expectedConversionIdentity,
    const fs::path& sourceRootOverride, vsg::ref_ptr<const vsg::Options> options)
{
    const auto entry = Json::parse(readBoundedFile(path, manifestLimit));
    if (entry.at("kind") != "geostudio-osgb-region" || entry.at("version") != 1)
        fail("invalid region entry identity");
    if (entry.at("conversionIdentity") != expectedConversionIdentity)
        fail("converter version changed; rebuild this dataset or open the source metadata.xml");
    const auto manifestPath = relativeFile(path.parent_path(), entry.at("manifest").get<std::string>());
    const auto manifest = inspectManifest(manifestPath, expectedConversionIdentity, sourceRootOverride);
    const auto found = std::find_if(manifest.regions.begin(), manifest.regions.end(), [&](const auto& region) {
        return region.id == entry.at("id").get<std::string>() && region.entry == path.filename().u8string() && region.full == entry.at("full").get<std::string>();
    });
    if (found == manifest.regions.end()) fail("region is no longer listed by the current dataset");
    verify(path, found->entryIdentity); validateSources(manifest, *found);
    return readNode(relativeFile(path.parent_path(), found->full), found->fullIdentity, manifest.sourceRoot, options, false);
}
}
