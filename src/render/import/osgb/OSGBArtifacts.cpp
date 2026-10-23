#include "OSGBArtifacts.h"
#include "OSGBFiles.h"
#include "OSGBSerialization.h"
#include <vsg/all.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>
#include <mutex>
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
using Clock = std::chrono::steady_clock;
constexpr uint64_t manifestLimit = 1024 * 1024;
constexpr const char* kind = "geostudio-osgb-page";
constexpr const char* schema = "vsg-page-v1";

struct Elapsed
{
    std::mutex& mutex;
    double& total;
    Clock::time_point begin = Clock::now();
    ~Elapsed()
    {
        const auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
        std::lock_guard<std::mutex> lock(mutex);
        total += elapsed;
    }
};
bool digest(const std::string& value)
{
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
fs::path relativePath(const fs::path& absolute, const fs::path& root)
{
    if (!absolute.is_absolute() || !pathWithin(absolute, root))
        throw std::runtime_error("OSGB cache dependency escapes source root");
    const auto relative = canonicalFilePath(absolute).lexically_relative(root);
    if (relative.empty() || relative == ".") throw std::runtime_error("OSGB cache dependency is not a file");
    return relative;
}
fs::path resolveRelative(const std::string& text, const fs::path& root)
{
    const auto relative = fs::u8path(text);
    if (text.empty() || relative.is_absolute() || relative.has_root_name() || text.find(':') != std::string::npos)
        throw std::runtime_error("OSGB cache contains an invalid relative path");
    for (const auto& part : relative)
        if (part == "." || part == "..") throw std::runtime_error("OSGB cache path contains traversal");
    const auto path = root / relative;
    if (!pathWithin(path, root)) throw std::runtime_error("OSGB cache path escapes source root");
    return path;
}

// 限制序列化输出的真实内存长度；VSG::write 的 bool 不足以反映 ostream 写失败。
class BoundedOutput : public std::streambuf
{
public:
    explicit BoundedOutput(uint64_t limit) : maximum(limit) {}
    std::string bytes;
protected:
    std::streamsize xsputn(const char* input, std::streamsize size) override
    {
        if (size < 0 || static_cast<uint64_t>(size) > maximum - bytes.size()) return 0;
        bytes.append(input, static_cast<size_t>(size));
        return size;
    }
    int_type overflow(int_type value) override
    {
        if (traits_type::eq_int_type(value, traits_type::eof())) return traits_type::not_eof(value);
        if (bytes.size() >= maximum) return traits_type::eof();
        bytes.push_back(traits_type::to_char_type(value));
        return value;
    }
private:
    uint64_t maximum;
};
std::string serialize(const vsg::Node& node, uint64_t maximum)
{
    BoundedOutput buffer(maximum);
    std::ostream output(&buffer);
    auto options = vsg::Options::create();
    options->extensionHint = ".vsgb";
    vsg::VSG writer;
    if (!writer.write(&node, output, options) || !output.good())
        throw std::runtime_error("OSGB page serialization failed or exceeded entry budget");
    return std::move(buffer.bytes);
}
vsg::ref_ptr<vsg::Node> deserialize(const std::string& bytes)
{
    auto options = vsg::Options::create();
    options->extensionHint = ".vsgb";
    vsg::VSG reader;
    // 内存入口没有 input.filename，避免 PagedLOD 将缓存目录前缀写入原始 OSGB 路径。
    auto object = reader.read(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), options);
    auto node = object.cast<vsg::Node>();
    if (!node) throw std::runtime_error("OSGB page cache does not contain a VSG node");
    return node;
}

// VSG 的 DescriptorImage 序列化保存 Sampler/Data，再构造 Image/View；额外验证会遗漏的语义。
struct Graph : vsg::Visitor
{
    std::set<vsg::Object*> visited;
    std::vector<vsg::PagedLOD*> pages;
    Json imageSemantics = Json::array();
    void apply(vsg::Object& object) override
    {
        if (visited.insert(&object).second) object.traverse(*this);
    }
    void apply(vsg::PagedLOD& page) override
    {
        if (!visited.insert(&page).second) return;
        if (page.children[0].node || page.pending)
            throw std::runtime_error("OSGB cache accepts only unloaded external PagedLOD children");
        pages.push_back(&page);
        if (page.children[1].node) page.children[1].node->accept(*this);
    }
    void apply(vsg::DescriptorImage& descriptor) override
    {
        if (!visited.insert(&descriptor).second) return;
        for (const auto& info : descriptor.imageInfoList)
        {
            if (!info || !info->imageView || !info->imageView->image || !info->imageView->image->data)
                throw std::runtime_error("OSGB cache image requires resident CPU data");
            const auto& view = *info->imageView;
            const auto& image = *view.image;
            const auto& data = *image.data;
            imageSemantics.push_back({
                {"binding", descriptor.dstBinding}, {"layout", info->imageLayout},
                {"view", {view.flags, view.viewType, view.format}},
                {"components", {view.components.r, view.components.g, view.components.b, view.components.a}},
                {"range", {view.subresourceRange.aspectMask, view.subresourceRange.baseMipLevel,
                    view.subresourceRange.levelCount, view.subresourceRange.baseArrayLayer, view.subresourceRange.layerCount}},
                {"image", {image.flags, image.imageType, image.format, image.extent.width, image.extent.height,
                    image.extent.depth, image.mipLevels, image.arrayLayers, image.samples, image.tiling,
                    image.usage, image.sharingMode, image.initialLayout}},
                {"queueFamilies", image.queueFamilyIndices},
                {"bytes", data.dataSize()}, {"dataHash", sha256Bytes(std::string_view(
                    static_cast<const char*>(data.dataPointer()), data.dataSize()))}});
        }
    }
};

// 图由调用者独占；记录每个路径的原值，任何异常和早退都还原借用对象。
struct RestorePaths
{
    std::vector<std::pair<vsg::PagedLOD*, vsg::Path>> originals;
    ~RestorePaths()
    {
        for (auto& entry : originals) entry.first->filename = entry.second;
    }
};

// 一个缓存目录只允许一个写者；退出关闭本次独占句柄，系统仅删除本次创建的锁文件。
class DirectoryLock
{
public:
    explicit DirectoryLock(const fs::path& root)
    {
#ifdef _WIN32
        const auto path = root / "osgb-pages.writer.lock";
        if (!pathWithin(path, root)) throw std::runtime_error("OSGB cache lock escapes output directory");
        const auto native = nativeFilePath(path);
        handle = CreateFileW(native.c_str(), GENERIC_WRITE | DELETE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            const auto error = GetLastError();
            throw std::runtime_error("OSGB page cache writer lock cannot be acquired (Win32=" + std::to_string(error) +
                ", pathChars=" + std::to_string(path.native().size()) + "): " + path.u8string());
        }
#else
        (void)root;
        throw std::runtime_error("OSGB page cache publication currently requires Windows");
#endif
    }
    ~DirectoryLock()
    {
#ifdef _WIN32
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
#endif
    }
private:
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
#endif
};
uint64_t directoryBytes(const fs::path& root, uint64_t maximum)
{
    uint64_t total = 0, count = 0;
    for (const auto& entry : fs::directory_iterator(nativeFilePath(root)))
    {
        if (++count > 100000 || !pathWithin(entry.path(), root) || !entry.is_regular_file())
            throw std::runtime_error("OSGB page cache directory contains unsupported entries");
        const auto size = entry.file_size();
        if (size > maximum - std::min(total, maximum)) return maximum;
        total += size;
    }
    return total;
}
}

struct OSGBArtifacts::Impl
{
    ArtifactContext context;
    mutable std::mutex mutex;
    Statistics totals;
    std::string disabled, identity;
    explicit Impl(ArtifactContext value) : context(std::move(value))
    {
        try
        {
            if (context.cacheRoot.empty() || !context.maxCacheBytes || !context.maxEntryBytes)
            { disabled = "OSGB page cache is disabled"; return; }
            if (context.generatorIdentity.empty() || context.coordinateIdentity.empty() ||
                context.maxEntryBytes > uint64_t{512} * 1024 * 1024 || context.maxEntryBytes > context.maxCacheBytes)
                throw std::runtime_error("OSGB page cache requires identities and an entry budget no larger than 512 MiB or cache budget");
            validateArtifactDirectory(context.cacheRoot, context.sourceRoot);
            context.sourceRoot = canonicalFilePath(context.sourceRoot);
            context.cacheRoot = canonicalFilePath(context.cacheRoot);
            const auto version = vsgGetVersion();
            identity = sha256Bytes(Json{{"schema", schema}, {"coordinate", context.coordinateIdentity},
                {"generator", context.generatorIdentity}, {"srgb", context.colorTexturesSRGB},
                {"vsg", {version.major, version.minor, version.patch, version.soversion}}}.dump());
        }
        catch (const std::exception& error) { disabled = error.what(); }
    }
    fs::path sourcePath(const fs::path& path) const
    {
        const auto absolute = path.is_absolute() ? path : context.sourceRoot / path;
        relativePath(absolute, context.sourceRoot);
        if (!fs::is_regular_file(nativeFilePath(absolute))) throw std::runtime_error("OSGB page source is missing");
        return canonicalFilePath(absolute);
    }
    std::string manifestName(const fs::path& path) const
    {
        return sha256Bytes(relativePath(path, context.sourceRoot).generic_u8string()) + ".page.json";
    }
    Json dependency(const fs::path& path) const
    {
        const auto absolute = sourcePath(path);
        return {{"path", relativePath(absolute, context.sourceRoot).generic_u8string()},
            {"bytes", fs::file_size(nativeFilePath(absolute))}, {"sha256", sha256File(absolute)}};
    }
    void verifyDependency(const Json& dependency) const
    {
        const auto path = resolveRelative(dependency.at("path").get<std::string>(), context.sourceRoot);
        const auto hash = dependency.at("sha256").get<std::string>();
        if (!digest(hash) || !fs::is_regular_file(nativeFilePath(path)) || fs::file_size(nativeFilePath(path)) != dependency.at("bytes").get<uint64_t>() ||
            sha256File(path) != hash) throw std::runtime_error("OSGB page source or external image changed");
    }
    Lookup loadPage(const fs::path& sourceFile, vsg::ref_ptr<const vsg::Options> pageOptions)
    {
        if (!disabled.empty()) return {LookupState::Disabled, {}, disabled};
        const auto source = sourcePath(sourceFile);
        validateArtifactDirectory(context.cacheRoot, context.sourceRoot);
        const auto manifest = context.cacheRoot / manifestName(source);
        if (!pathWithin(manifest, context.cacheRoot)) throw std::runtime_error("OSGB cache manifest escapes output directory");
        if (!fs::exists(nativeFilePath(manifest))) return {LookupState::Miss, {}, {}};
        const auto metadata = Json::parse(readBoundedFile(manifest, manifestLimit));
        if (metadata.at("kind") != kind || metadata.at("schema") != schema || metadata.at("identity") != identity ||
            metadata.at("source").at("path") != relativePath(source, context.sourceRoot).generic_u8string())
            throw std::runtime_error("OSGB page cache identity changed");
        verifyDependency(metadata.at("source"));
        const auto& dependencies = metadata.at("images");
        if (!dependencies.is_array() || dependencies.size() > 4096) throw std::runtime_error("OSGB cache has too many image dependencies");
        for (const auto& item : dependencies) verifyDependency(item);
        const auto blobHash = metadata.at("blobHash").get<std::string>();
        if (!digest(blobHash)) throw std::runtime_error("OSGB cache blob hash is invalid");
        const auto blob = context.cacheRoot / (blobHash + ".vsgb");
        if (!pathWithin(blob, context.cacheRoot) || !fs::is_regular_file(nativeFilePath(blob)) ||
            fs::file_size(nativeFilePath(blob)) != metadata.at("blobBytes").get<uint64_t>())
            throw std::runtime_error("OSGB page cache blob is missing or incomplete");
        const auto bytes = readBoundedFile(blob, context.maxEntryBytes);
        if (sha256Bytes(bytes) != blobHash) throw std::runtime_error("OSGB page cache blob checksum failed");
        auto node = deserialize(bytes);
        Graph graph;
        node->accept(graph);
        const auto& paths = metadata.at("pages");
        if (!paths.is_array() || paths.size() != graph.pages.size()) throw std::runtime_error("OSGB page cache descendant table mismatch");
        auto rebound = pageOptions ? vsg::Options::create(*pageOptions) : vsg::Options::create();
        for (size_t i = 0; i < graph.pages.size(); ++i)
        {
            auto& page = *graph.pages[i];
            const auto path = paths.at(i).get<std::string>();
            if (page.filename.string() != path) throw std::runtime_error("OSGB page cache descendant path mismatch");
            if (!path.empty()) page.filename = resolveRelative(path, context.sourceRoot).u8string();
            page.options = rebound;
        }
        { std::lock_guard<std::mutex> lock(mutex); totals.bytesRead += bytes.size(); }
        return {LookupState::Hit, node, {}};
    }
};

OSGBArtifacts::OSGBArtifacts(ArtifactContext context) : _impl(std::make_unique<Impl>(std::move(context))) {}
OSGBArtifacts::~OSGBArtifacts() = default;

OSGBArtifacts::Lookup OSGBArtifacts::load(const fs::path& sourceFile, vsg::ref_ptr<const vsg::Options> pageOptions)
{
    Elapsed elapsed{_impl->mutex, _impl->totals.loadMilliseconds};
    try
    {
        auto result = _impl->loadPage(sourceFile, pageOptions);
        std::lock_guard<std::mutex> lock(_impl->mutex);
        if (result.state == LookupState::Hit) ++_impl->totals.hits;
        else ++_impl->totals.misses;
        return result;
    }
    catch (const std::exception& error)
    {
        std::lock_guard<std::mutex> lock(_impl->mutex);
        ++_impl->totals.invalid; ++_impl->totals.misses;
        return {LookupState::Invalid, {}, error.what()};
    }
    catch (...)
    {
        std::lock_guard<std::mutex> lock(_impl->mutex);
        ++_impl->totals.invalid; ++_impl->totals.misses;
        return {LookupState::Invalid, {}, "Unknown OSGB cache read failure"};
    }
}

OSGBArtifacts::StoreResult OSGBArtifacts::store(const ArtifactPage& input, vsg::Node& privatePage)
{
    Elapsed elapsed{_impl->mutex, _impl->totals.storeMilliseconds};
    const auto& context = _impl->context;
    if (!_impl->disabled.empty()) return {StoreState::Disabled, _impl->disabled};
    try
    {
        const auto source = _impl->sourcePath(input.sourceFile);
        validateArtifactDirectory(context.cacheRoot, context.sourceRoot);
        fs::create_directories(nativeFilePath(context.cacheRoot));
        validateArtifactDirectory(context.cacheRoot, context.sourceRoot);
        DirectoryLock writer(context.cacheRoot);
        try
        {
            if (_impl->loadPage(source, {}).state == LookupState::Hit)
            {
                std::lock_guard<std::mutex> lock(_impl->mutex);
                ++_impl->totals.alreadyPresent; return {StoreState::AlreadyPresent, {}};
            }
        }
        catch (const std::exception&) { /* 过期缓存允许发布新 manifest；旧 blob 保留并计入预算。 */ }
        Json metadata{{"kind", kind}, {"schema", schema}, {"identity", _impl->identity},
            {"source", _impl->dependency(source)}, {"images", Json::array()}, {"pages", Json::array()}};
        if (!input.expectedSourceSha256.empty() && metadata["source"]["sha256"] != input.expectedSourceSha256)
            throw std::runtime_error("OSGB source changed after decode; old page was not cached");
        if (input.externalImages.size() > 4096) throw std::runtime_error("OSGB page has too many external images");
        std::set<std::string> seen;
        for (const auto& image : input.externalImages)
        {
            auto item = _impl->dependency(image);
            const auto expected = input.expectedImageHashes.find(_impl->sourcePath(image));
            if ((!input.expectedImageHashes.empty() && expected == input.expectedImageHashes.end()) ||
                (expected != input.expectedImageHashes.end() && item["sha256"] != expected->second))
                throw std::runtime_error("OSGB external image changed after decode; old page was not cached");
            if (seen.insert(item.at("path").get<std::string>()).second) metadata["images"].push_back(std::move(item));
        }
        if (!input.expectedImageHashes.empty() && seen.size() != input.expectedImageHashes.size())
            throw std::runtime_error("OSGB page cache image dependency observations are incomplete");
        Graph original;
        privatePage.accept(original);
        RestorePaths restore;
        OSGBPlainImages plain(privatePage, context.maxEntryBytes);
        for (auto* page : original.pages)
        {
            restore.originals.emplace_back(page, page->filename);
            if (page->filename)
            {
                auto path = fs::u8path(page->filename.string());
                if (!path.is_absolute()) path = source.parent_path() / path;
                page->filename = relativePath(path, context.sourceRoot).generic_u8string();
            }
            metadata["pages"].push_back(page->filename.string());
        }
        const auto bytes = serialize(privatePage, context.maxEntryBytes);
        // 往返一致性由格式回归覆盖；生产写入不再同时解码第二份完整页面。
        // 继续保留源/依赖强校验、写后摘要校验以及读取时的结构检查。
        // 发布前复核身份，避免序列化期间源文件发生变化而留下不一致制品。
        _impl->verifyDependency(metadata.at("source"));
        for (const auto& image : metadata.at("images")) _impl->verifyDependency(image);
        const auto blobHash = sha256Bytes(bytes);
        metadata["blobHash"] = blobHash;
        metadata["blobBytes"] = bytes.size();
        const auto manifest = metadata.dump();
        if (manifest.size() > manifestLimit) throw std::runtime_error("OSGB page cache manifest exceeded limit");
        const auto blobName = blobHash + ".vsgb";
        const auto blob = context.cacheRoot / blobName;
        const bool existingBlob = fs::exists(nativeFilePath(blob));
        if (existingBlob && (!pathWithin(blob, context.cacheRoot) || sha256File(blob) != blobHash))
            throw std::runtime_error("OSGB content-addressed blob has unexpected content");
        const auto used = directoryBytes(context.cacheRoot, context.maxCacheBytes);
        const uint64_t additional = (existingBlob ? 0 : bytes.size()) + manifest.size();
        if (used >= context.maxCacheBytes || additional > context.maxCacheBytes - used)
        {
            std::lock_guard<std::mutex> lock(_impl->mutex);
            ++_impl->totals.budgetExceeded;
            return {StoreState::BudgetExceeded, "OSGB page cache budget reached; source loading continues"};
        }
        // 运行缓存可重建，不强制每页刷盘；Close、内容校验和原子入口发布仍保留。
        // 断电后不保证缓存持久，读取损坏时必须 soft miss 并重新读取源 OSGB。
        if (!existingBlob) publishArtifactText(context.cacheRoot, context.sourceRoot, blobName, bytes, false);
        // 先验证落盘 blob，再发布唯一入口；中断最多留下受预算管理的孤立 blob/pending。
        if (sha256File(blob) != blobHash) throw std::runtime_error("OSGB page cache persisted checksum failed");
        const auto manifestName = _impl->manifestName(source);
        const auto previous = context.cacheRoot / manifestName;
        if (fs::exists(nativeFilePath(previous)))
        {
            const auto old = Json::parse(readBoundedFile(previous, manifestLimit));
            if (old.at("kind") != kind || old.at("schema") != schema)
                throw std::runtime_error("OSGB cache refuses to replace an unrecognized manifest");
        }
        publishArtifactText(context.cacheRoot, context.sourceRoot, manifestName, manifest, false);
        {
            std::lock_guard<std::mutex> lock(_impl->mutex);
            ++_impl->totals.stored;
            _impl->totals.bytesWritten += (existingBlob ? 0 : bytes.size()) + manifest.size();
        }
        return {StoreState::Stored, {}};
    }
    catch (const std::exception& error)
    {
        std::lock_guard<std::mutex> lock(_impl->mutex);
        ++_impl->totals.failures; return {StoreState::Failed, error.what()};
    }
    catch (...)
    {
        std::lock_guard<std::mutex> lock(_impl->mutex);
        ++_impl->totals.failures; return {StoreState::Failed, "Unknown OSGB cache write failure"};
    }
}

OSGBArtifacts::Statistics OSGBArtifacts::statistics() const
{
    std::lock_guard<std::mutex> lock(_impl->mutex);
    return _impl->totals;
}
}
