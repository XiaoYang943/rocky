#include <geostudio/native/render/import/osgb/OSGB.h>
#include <geostudio/native/render/import/osgb/OSGBPaging.h>
#include "OSGBResources.h"
#include "OSGBLOD.h"
#include "OSGBIndex.h"
#include "OSGBDecode.h"
#include "OSGBFiles.h"
#include "OSGBDataset.h"
#include "OSGBOverview.h"
#include "OSGBArtifacts.h"
#include <iomanip>
#include <osg2vsg/convert.h>
#include <osg/Geometry>
#include <osg/MatrixTransform>
#include <osg/PagedLOD>
#include <osg/ProxyNode>
#include <osg/Texture2D>
#include <osgDB/ReadFile>
#include <osgDB/Registry>
#include <osgDB/Callbacks>
#include <vsg/all.h>
#include <cpl_minixml.h>
#include <proj.h>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <optional>
#include <sstream>
#include <set>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <map>
#ifdef _WIN32
#include <windows.h>
#endif

using namespace geostudio::native::io;
namespace fs = std::filesystem;
namespace
{
constexpr const char* contextKey = "geostudio.osgb.context";

// 上下文只读共享给分页请求；坐标操作由每个工作线程独占。
struct Context : vsg::Inherit<vsg::Object, Context>
{
    std::string srs;
    vsg::dvec3 origin{}, anchor{};
    fs::path sourceRoot;
};

struct Projection
{
    PJ_CONTEXT* context = proj_context_create();
    PJ* operation = nullptr;
    explicit Projection(const std::string& srs)
    {
        if (srs.empty()) return;
        auto raw = proj_create_crs_to_crs(context, srs.c_str(), "EPSG:4978", nullptr);
        if (raw) { operation = proj_normalize_for_visualization(context, raw); proj_destroy(raw); }
        if (!operation) { proj_context_destroy(context); context = nullptr; throw std::runtime_error("Cannot transform OSGB SRS to ECEF: " + srs); }
    }
    ~Projection() { proj_destroy(operation); proj_context_destroy(context); }
    // PROJ 输入使用可视化轴序，返回有限 ECEF；失败抛出而不产生坏坐标。
    vsg::dvec3 apply(const vsg::dvec3& p) const
    {
        if (!operation) return p;
        proj_errno_reset(operation);
        auto q = proj_trans(operation, PJ_FWD, proj_coord(p.x, p.y, p.z, 0));
        if (proj_errno(operation) || !std::isfinite(q.xyz.x) || !std::isfinite(q.xyz.y) || !std::isfinite(q.xyz.z))
            throw std::runtime_error("OSGB coordinate transformation failed");
        return {q.xyz.x, q.xyz.y, q.xyz.z};
    }
};

// 每个工作线程独占坐标操作，按 CRS 复用；不在线程之间共享 PROJ 的可变错误状态。
Projection& threadProjection(const std::string& srs)
{
    thread_local std::map<std::string, std::unique_ptr<Projection>> operations;
    auto found = operations.find(srs);
    if (found == operations.end())
    {
        if (operations.size() >= 8) operations.clear();
        found = operations.emplace(srs, std::make_unique<Projection>(srs)).first;
    }
    return *found->second;
}

// 只接受常规静态倾斜摄影节点；未知分页语义必须显式失败。
struct Prepare : osg::NodeVisitor
{
    const Context& context;
    Projection& projection;
    fs::path directory;
    std::set<osg::Geometry*> visited;
    size_t geometries = 0;
    struct Range
    {
        bool pixels;
        double split;
        vsg::dvec4 sphere, distanceSphere;
    };
    std::map<std::string, Range> ranges;
    Prepare(const Context& c, const fs::path& dir) : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN), context(c), projection(threadProjection(c.srs)), directory(dir) {}
    // 有状态的自定义 Shader 和缺失纹理不能悄悄降级为无纹理几何。
    void check(const osg::Node& node) const
    {
        if (node.getUpdateCallback() || node.getEventCallback() || node.getCullCallback())
            throw std::runtime_error("OSGB callbacks are not supported by the static reader");
        const auto* state = node.getStateSet();
        if (!state) return;
        if (state->getAttribute(osg::StateAttribute::PROGRAM))
            throw std::runtime_error("OSGB custom programs require a dedicated Vulkan material adapter");
        for (unsigned unit = 0; unit < state->getTextureAttributeList().size(); ++unit)
        {
            const auto* texture = dynamic_cast<const osg::Texture*>(state->getTextureAttribute(unit, osg::StateAttribute::TEXTURE));
            if (!texture) continue;
            if (!dynamic_cast<const osg::Texture2D*>(texture) || !texture->getImage(0) || !texture->getImage(0)->data())
                throw std::runtime_error("OSGB requires a decoded Texture2D image");
        }
    }
    void apply(osg::Node& node) override { check(node); traverse(node); }
    vsg::dvec3 point(const osg::Vec3d& p) const
    {
        const vsg::dvec3 value(p.x(), p.y(), p.z());
        return context.srs.empty() ? value : projection.apply(value + context.origin) - context.anchor;
    }
    void apply(osg::Transform& transform) override
    {
        check(transform);
        if (!context.srs.empty())
        {
            auto matrix = dynamic_cast<osg::MatrixTransform*>(&transform);
            if (!matrix || !matrix->getMatrix().isIdentity())
                throw std::runtime_error("Georeferenced OSGB with local Transform is not supported yet");
        }
        traverse(transform);
    }
    void apply(osg::ProxyNode&) override { throw std::runtime_error("OSGB ProxyNode requires explicit dataset roots; unsupported external reference"); }
    void apply(osg::PagedLOD& lod) override
    {
        check(lod);
        if (lod.getNumRanges() != 2 || lod.getNumChildren() != 1 || !lod.getFileName(0).empty() || lod.getFileName(1).empty())
            throw std::runtime_error("OSGB PagedLOD requires one resident coarse child and one external fine child in two ranges");
        const bool pixels = lod.getRangeMode() == osg::LOD::PIXEL_SIZE_ON_SCREEN;
        if ((pixels && (lod.getMinRange(0) != 0 || lod.getMaxRange(0) != lod.getMinRange(1))) ||
            (!pixels && (lod.getMinRange(1) != 0 || lod.getMaxRange(1) != lod.getMinRange(0))))
            throw std::runtime_error("OSGB PagedLOD ranges are overlapping or discontinuous");
        const float split = pixels ? lod.getMinRange(1) : lod.getMinRange(0);
        const float outer = lod.getMaxRange(pixels ? 1 : 0);
        // HL 使用 1e30 作为无穷远哨兵；同时接受 FLT_MAX，不忽略有实际意义的有限上界。
        if (!std::isfinite(split) || split <= 0 || !std::isfinite(outer) || outer < 1e30f)
            throw std::runtime_error("OSGB PagedLOD requires a positive split and an unbounded outer range (>=1e30): split=" + std::to_string(split) + ", outer=" + std::to_string(outer));
        auto base = lod.getDatabasePath().empty() ? directory : fs::u8path(lod.getDatabasePath());
        if (base.is_relative()) base = directory / base;
        const auto child = (base / fs::u8path(lod.getFileName(1))).lexically_normal();
        lod.setFileName(1, child.u8string());
        // 源 LOD 的度量球与安全剔除球分离；投影包围盒的余量不能放大 LOD 像素。
        const auto sourceBound = lod.getBound();
        const auto lodCenter = point(sourceBound.center());
        const auto distanceCenter = point(lod.getCenter());
        ranges[child.u8string()] = {pixels, split,
            {lodCenter.x, lodCenter.y, lodCenter.z, sourceBound.radius()},
            {distanceCenter.x, distanceCenter.y, distanceCenter.z, sourceBound.radius()}};
        lod.setDatabasePath("");
        if (!context.srs.empty())
        {
            auto bound = sphere(lod.getBound());
            lod.setCenterMode(osg::LOD::USER_DEFINED_CENTER);
            lod.setCenter(osg::Vec3(bound.x, bound.y, bound.z));
            lod.setRadius(bound.w);
        }
        traverse(lod);
    }
    // 球包围盒角点投影并留余量；大范围/跨投影区模型不在首版契约内。
    vsg::dvec4 sphere(const osg::BoundingSphere& b) const
    {
        if (!b.valid() || !std::isfinite(b.radius())) throw std::runtime_error("OSGB has invalid bounds");
        auto center = point(b.center());
        double radius = b.radius();
        if (!context.srs.empty())
        {
            if (radius > 10000.0) throw std::runtime_error("OSGB root radius exceeds the 10 km projection bound limit");
            radius = 0;
            for (int x : {-1, 1}) for (int y : {-1, 1}) for (int z : {-1, 1})
                radius = std::max(radius, vsg::length(point(osg::Vec3d(b.center()) + osg::Vec3d(x,y,z) * b.radius()) - center));
            radius = radius * 1.01 + 1.0;
        }
        return {center.x, center.y, center.z, radius};
    }
    void apply(osg::Geometry& geometry) override
    {
        check(geometry);
        if (!visited.insert(&geometry).second) return;
        if (!geometry.getVertexArray() || !geometry.getVertexArray()->getNumElements() || !geometry.getNumPrimitiveSets())
            throw std::runtime_error("OSGB contains empty geometry");
        ++geometries;
        if (context.srs.empty()) return;
        auto floats = dynamic_cast<osg::Vec3Array*>(geometry.getVertexArray());
        auto doubles = dynamic_cast<osg::Vec3dArray*>(geometry.getVertexArray());
        if (!floats && !doubles) throw std::runtime_error("OSGB vertex array must be Vec3 or Vec3d");
        auto output = new osg::Vec3Array;
        output->reserve(geometry.getVertexArray()->getNumElements());
        std::vector<PJ_COORD> coordinates(geometry.getVertexArray()->getNumElements());
        for (unsigned i = 0; i < coordinates.size(); ++i)
        {
            const auto p = floats ? osg::Vec3d((*floats)[i]) : (*doubles)[i];
            coordinates[i] = proj_coord(p.x() + context.origin.x, p.y() + context.origin.y, p.z() + context.origin.z, 0);
        }
        proj_errno_reset(projection.operation);
        if (proj_trans_array(projection.operation, PJ_FWD, coordinates.size(), coordinates.data()) || proj_errno(projection.operation))
            throw std::runtime_error("OSGB coordinate batch transformation failed");
        for (const auto& p : coordinates)
        {
            if (!std::isfinite(p.xyz.x) || !std::isfinite(p.xyz.y) || !std::isfinite(p.xyz.z))
                throw std::runtime_error("OSGB coordinate batch contains non-finite vertices");
            const auto value = vsg::dvec3(p.xyz.x, p.xyz.y, p.xyz.z) - context.anchor;
            output->push_back(osg::Vec3(value.x, value.y, value.z));
        }
        geometry.setVertexArray(output);
        // 倾斜摄影纹理已包含光照；投影后采用 Unlit，不沿用源坐标系法线照明。
        geometry.getOrCreateStateSet()->setMode(GL_LIGHTING, osg::StateAttribute::OFF | osg::StateAttribute::OVERRIDE);
        geometry.dirtyBound();
    }
};

// 保存原始分页范围，使派生页可按宿主当前视口重新求阈值。
void annotateRanges(vsg::Node& node, const Prepare& prepare)
{
    struct Ranges : vsg::Visitor
    {
        const std::map<std::string, Prepare::Range>& ranges;
        explicit Ranges(const decltype(ranges)& values) : ranges(values) {}
        void apply(vsg::Object& object) override { object.traverse(*this); }
        void apply(vsg::PagedLOD& page) override
        {
            auto found = ranges.find(page.filename.string());
            if (found != ranges.end())
            {
                page.setValue(found->second.pixels ? "osgb.pixelSplit" : "osgb.distanceSplit", found->second.split);
                page.setValue("osgb.lodSphere", found->second.sphere);
                page.setValue("osgb.distanceSphere", found->second.distanceSphere);
            }
            page.traverse(*this);
        }
    } ranges(prepare.ranges);
    node.accept(ranges);
}

std::string conversionIdentity(bool srgb)
{
    return std::string("native-osgb-v3:ecef-v2:source-lod-v3:vsg-e4718bf:osg2vsg-8494fc6:") + (srgb ? "srgb" : "linear");
}

// metadata 只在入口解析；子请求继承确定的 CRS/原点，避免重复推断。
vsg::ref_ptr<Context> readContext(const fs::path& metadata)
{
    auto context = Context::create();
    if (metadata.empty()) return context;
    context->sourceRoot = metadata.parent_path();
    if (fs::file_size(metadata) > 1024 * 1024) throw std::runtime_error("OSGB metadata exceeds 1 MiB");
    std::unique_ptr<CPLXMLNode, decltype(&CPLDestroyXMLNode)> xml(CPLParseXMLFile(metadata.u8string().c_str()), CPLDestroyXMLNode);
    if (!xml) throw std::runtime_error("Invalid OSGB metadata.xml");
    auto root = CPLGetXMLNode(xml.get(), "=ModelMetadata");
    if (!root) throw std::runtime_error("OSGB metadata requires ModelMetadata");
    context->srs = CPLGetXMLValue(root, "SRS", "");
    std::string origin = CPLGetXMLValue(root, "SRSOrigin", "");
    std::replace(origin.begin(), origin.end(), ',', ' ');
    std::istringstream input(origin);
    std::string extra;
    if (context->srs.empty() || !(input >> context->origin.x >> context->origin.y >> context->origin.z) || (input >> extra))
        throw std::runtime_error("OSGB metadata requires SRS and three finite SRSOrigin coordinates");
    if (!std::isfinite(context->origin.x) || !std::isfinite(context->origin.y) || !std::isfinite(context->origin.z))
        throw std::runtime_error("Invalid OSGB SRSOrigin");
    Projection projection(context->srs);
    auto crs = proj_create(projection.context, context->srs.c_str());
    const auto type = crs ? proj_get_type(crs) : PJ_TYPE_UNKNOWN;
    proj_destroy(crs);
    if (type != PJ_TYPE_PROJECTED_CRS) throw std::runtime_error("OSGB metadata currently requires a projected CRS with metre units");
    auto source = proj_create(projection.context, context->srs.c_str());
    auto axes = proj_crs_get_coordinate_system(projection.context, source);
    double unit = 0;
    const bool metres = axes && proj_cs_get_axis_info(projection.context, axes, 0, nullptr, nullptr, nullptr, &unit, nullptr, nullptr, nullptr) && std::abs(unit - 1.0) < 1e-12;
    proj_destroy(axes); proj_destroy(source);
    if (!metres) throw std::runtime_error("OSGB metadata projected coordinates must use metres");
    context->anchor = projection.apply(context->origin);
    return context;
}
}

struct OSGB::Impl
{
    std::atomic<bool> closed{false};
    mutable std::mutex mutex;
    std::string error;
    Status statistics;
    Settings settings;
    bool settingsFrozen = false;
    bool pageCacheEnabled = false;
    Performance performance;
    uint64_t initialDraws = 0, initialPages = 0;
    detail::OSGBResources resources;
    std::unique_ptr<detail::OSGBDecode> decoder = std::make_unique<detail::OSGBDecode>();
    vsg::ref_ptr<OSGBPagingToken> token = OSGBPagingToken::create();
    std::mutex cacheMutex;
    std::map<std::string, std::shared_ptr<detail::OSGBArtifacts>> caches;
    std::shared_ptr<detail::OSGBArtifacts> cache(const Context& context)
    {
        if (!pageCacheEnabled || settings.cacheDirectory.empty()) return {};
        std::ostringstream identity;
        identity << std::setprecision(17) << context.srs << ':' << context.origin.x << ':' << context.origin.y << ':' << context.origin.z
            << ':' << context.anchor.x << ':' << context.anchor.y << ':' << context.anchor.z << ":ecef-v2:lod-v3";
        const auto key = context.sourceRoot.generic_u8string() + '|' + identity.str();
        std::lock_guard<std::mutex> lock(cacheMutex);
        auto& value = caches[key];
        if (!value)
        {
            detail::ArtifactContext config;
            config.sourceRoot = context.sourceRoot;
            config.cacheRoot = fs::u8path(settings.cacheDirectory) / detail::sha256Bytes(context.sourceRoot.generic_u8string()) / "pages";
            config.coordinateIdentity = identity.str(); config.generatorIdentity = conversionIdentity(settings.colorTexturesSRGB);
            config.colorTexturesSRGB = settings.colorTexturesSRGB;
            config.maxCacheBytes = settings.maxCacheBytes; config.maxEntryBytes = settings.maxCacheEntryBytes;
            value = std::make_shared<detail::OSGBArtifacts>(config);
        }
        return value;
    }
    void process(vsg::Node& node)
    {
        resources.process(node, settings.colorTexturesSRGB ? detail::OSGBColorSpace::SRGB : detail::OSGBColorSpace::Linear);
        const auto values = resources.statistics();
        std::lock_guard<std::mutex> lock(mutex);
        performance.resourceMilliseconds = values.resourceProcessMilliseconds;
        performance.shaderMilliseconds = values.shaderCompileMilliseconds;
        performance.pipelinesCreated = values.pipelinesCreated; performance.pipelineHits = values.pipelineHits;
        performance.shadersCreated = values.shaderModulesCreated; performance.shaderHits = values.shaderHits;
        performance.samplersCreated = values.samplersCreated; performance.samplerHits = values.samplerHits;
        performance.colorImagesConverted = values.colorImagesConverted;
    }
    template<typename F> auto timed(double Performance::* field, F&& function)
    {
        const auto start = std::chrono::steady_clock::now();
        auto result = function();
        std::lock_guard<std::mutex> lock(mutex);
        performance.*field += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        return result;
    }
    // 每个已接受的读取恰好以成功、失败或关闭取消结束，计数在同一锁内更新。
    struct Attempt
    {
        Impl& owner;
        bool finished = false;
        explicit Attempt(Impl& value) : owner(value)
        {
            std::lock_guard<std::mutex> lock(owner.mutex);
            owner.settingsFrozen = true;
            ++owner.statistics.readRequests;
            ++owner.statistics.reading;
        }
        void finish(bool success)
        {
            std::lock_guard<std::mutex> lock(owner.mutex);
            if (owner.closed) ++owner.statistics.readCancelled;
            else if (success) ++owner.statistics.readCompleted;
            else ++owner.statistics.readFailures;
            --owner.statistics.reading;
            finished = true;
        }
        ~Attempt()
        {
            if (!finished)
            {
                std::lock_guard<std::mutex> lock(owner.mutex);
                ++owner.statistics.readCancelled;
                --owner.statistics.reading;
            }
        }
    };
};
OSGB::OSGB() : _impl(new Impl) {}
OSGB::~OSGB() = default;
std::future<vsg::ref_ptr<vsg::Node>> OSGB::readAsync(const vsg::Path& filename, vsg::ref_ptr<const vsg::Options> options)
{
    { std::lock_guard<std::mutex> lock(_impl->mutex); _impl->settingsFrozen = true; }
    auto owner = vsg::ref_ptr<OSGB>(this);
    return std::async(std::launch::async, [owner, filename, options] { return owner->read(filename, options).cast<vsg::Node>(); });
}
bool OSGB::available() { return true; }
void OSGB::close()
{
    std::lock_guard<std::mutex> lock(_impl->mutex);
    _impl->closed = true; _impl->token->close(); _impl->decoder->close();
}
void OSGB::configure(const Settings& settings)
{
    if (!settings.maxReadInFlight || settings.maxReadInFlight > 4) throw std::invalid_argument("OSGB maxReadInFlight requires 1..4");
    std::lock_guard<std::mutex> lock(_impl->mutex);
    if (_impl->settingsFrozen || _impl->closed) throw std::logic_error("OSGB settings must be configured before reading");
    _impl->settings = settings;
    detail::OSGBDecode::Settings decodeSettings;
    decodeSettings.maxInFlight = settings.maxReadInFlight;
    _impl->decoder = std::make_unique<detail::OSGBDecode>(decodeSettings);
}
void OSGB::enablePageCache(bool enabled)
{
    std::lock_guard lock(_impl->mutex);
    if (_impl->settingsFrozen || _impl->closed) throw std::logic_error("OSGB cache policy must be configured before reading");
    _impl->pageCacheEnabled = enabled;
}
void OSGB::updateView(vsg::Node* attachedRoot, double height, double projectionY)
{
    if (!std::isfinite(height) || height <= 0 || !std::isfinite(projectionY) || projectionY <= 0)
        throw std::invalid_argument("OSGB view metrics must be finite and positive");
    const auto oldHeight = _impl->token->viewportHeight.exchange(height);
    const auto oldProjection = _impl->token->projectionY.exchange(projectionY);
    if (attachedRoot && (oldHeight != height || oldProjection != projectionY)) adjustOSGBLOD(*attachedRoot, height, projectionY);
}
OSGB::Performance OSGB::performance() const
{
    const auto decoder = _impl->decoder->statistics();
    std::lock_guard<std::mutex> lock(_impl->mutex);
    auto result = _impl->performance;
    result.sourceIoMilliseconds = decoder.ioMilliseconds;
    result.decodeLockWaitMilliseconds = decoder.compatibilityLockWaitMilliseconds;
    return result;
}
std::string OSGB::error() const { std::lock_guard<std::mutex> lock(_impl->mutex); return _impl->error; }
OSGB::Status OSGB::status(const vsg::Node* root, uint64_t frameCount) const
{
    Status result;
    {
        std::lock_guard<std::mutex> lock(_impl->mutex);
        result = _impl->statistics;
        result.error = _impl->error;
    }
    result.closed = _impl->closed;
    if (!root)
    {
        const auto metrics = _impl->token->metrics();
        result.queuedPages = metrics.queued; result.readingPages = metrics.reading;
        result.compilingPages = metrics.compiling; result.mergePendingPages = metrics.ready;
        result.attachedPages = metrics.residentPages;
        std::lock_guard<std::mutex> lock(_impl->mutex);
        result.pagedNodes = _impl->initialPages;
        result.residentDraws = _impl->initialDraws + metrics.residentDraws;
        return result;
    }
    struct Inspect : vsg::ConstVisitor
    {
        Status& result;
        uint64_t frame;
        std::set<const vsg::Object*> seen;
        Inspect(Status& value, uint64_t count) : result(value), frame(count) {}
        void apply(const vsg::Object& object) override
        {
            if (seen.insert(&object).second) object.traverse(*this);
        }
        void apply(const vsg::Draw&) override { ++result.residentDraws; }
        void apply(const vsg::DrawIndexed&) override { ++result.residentDraws; }
        void apply(const vsg::VertexDraw&) override { ++result.residentDraws; }
        void apply(const vsg::VertexIndexDraw&) override { ++result.residentDraws; }
        void apply(const vsg::PagedLOD& page) override
        {
            if (!seen.insert(&page).second) return;
            ++result.pagedNodes;
            if (page.children[0].node) ++result.attachedPages;
            const auto state = page.requestStatus.load();
            if (state == vsg::PagedLOD::ReadRequest) ++result.queuedPages;
            else if (state == vsg::PagedLOD::Reading) ++result.readingPages;
            else if (state == vsg::PagedLOD::Compiling) ++result.compilingPages;
            else if (state == vsg::PagedLOD::MergeRequest || state == vsg::PagedLOD::Merging) ++result.mergePendingPages;
            else if (state == vsg::PagedLOD::NoRequest && !page.children[0].node && page.loadAttempts > 0 && page.frameNextLoadAttempt > frame)
            {
                ++result.deferredPages;
                if (result.deferredFiles.size() < 4) result.deferredFiles.push_back(page.filename.string());
            }
            page.traverse(*this);
        }
    } inspect(result, frameCount);
    if (root) root->accept(inspect);
    return result;
}
bool OSGB::getFeatures(Features& features) const
{
    features.extensionFeatureMap[".osgb"] = READ_FILENAME;
    features.extensionFeatureMap[".xml"] = READ_FILENAME;
    features.extensionFeatureMap[".shp"] = READ_FILENAME;
    features.extensionFeatureMap[".osgbset"] = READ_FILENAME;
    features.extensionFeatureMap[".osgbregion"] = READ_FILENAME;
    return true;
}

OSGB::BuildResult OSGB::buildDataset(const vsg::Path& input, const BuildSettings& settings)
{
    if (settings.outputDirectory.empty()) throw std::invalid_argument("OSGB build requires an independent output directory");
    if (!settings.rootsPerRegion || settings.rootsPerRegion > 16 || !settings.textureMaxDimension || settings.textureMaxDimension > 16384 ||
        !std::isfinite(settings.triangleRatio) || settings.triangleRatio <= 0.0 || settings.triangleRatio > 1.0 ||
        !std::isfinite(settings.maximumSimplificationError) || settings.maximumSimplificationError < 0.0 ||
        !std::isfinite(settings.regionScreenHeightRatio) || settings.regionScreenHeightRatio <= 0.0 ||
        !settings.maxWorkingBytes || !settings.maxOutputBytes || settings.maxOutputBytes > 2ull * 1024 * 1024 * 1024)
        throw std::invalid_argument("Invalid OSGB dataset build settings");
    auto cancelled = [&] { return settings.cancelled && settings.cancelled(); };
    auto checkCancelled = [&] { if (cancelled()) throw std::runtime_error("OSGB dataset build cancelled"); };
    detail::IndexReadOptions indexOptions;
    indexOptions.sourceRootOverride = fs::u8path(settings.sourceRoot);
    auto index = detail::readDatasetIndex(fs::u8path(input.string()), indexOptions);
    const auto output = fs::u8path(settings.outputDirectory);
    detail::validateArtifactDirectory(output, index.sourceRoot);
    auto context = readContext(index.sourceRoot / "metadata.xml");
    detail::OSGBDecode decoder;
    Prepare bounds(*context, index.sourceRoot);
    // 目录首次构建只保留范围，不把全部已解码根积累在内存中。
    for (auto& record : index.roots)
    {
        checkCancelled();
        if (record.footprint || record.localEcefSphere) continue;
        const auto file = index.sourceRoot / fs::u8path(record.relativePath);
        const auto source = decoder.read(file);
        const auto sphere = bounds.sphere(source.node->getBound());
        record.localEcefSphere = std::array<double,4>{sphere.x, sphere.y, sphere.z, sphere.w};
    }
    detail::DatasetBuildSettings build;
    build.outputDirectory = output;
    build.rootsPerRegion = settings.rootsPerRegion;
    build.maxOutputBytes = settings.maxOutputBytes;
    build.maxWorkingBytes = settings.maxWorkingBytes;
    build.regionScreenHeightRatio = settings.regionScreenHeightRatio;
    build.conversionIdentity = conversionIdentity(settings.colorTexturesSRGB);
    std::ostringstream identity;
    identity << std::setprecision(17) << "overview-v1:" << settings.textureMaxDimension << ':'
        << settings.triangleRatio << ':' << settings.maximumSimplificationError;
    build.overviewIdentity = identity.str();
    auto options = vsg::Options::create();
    options->readerWriters = {vsg::glsl::create(), vsg::spirv::create(), vsg::VSG::create()};
    detail::OSGBResources resources;
    uint32_t completed = 0;
    const auto result = detail::buildDataset(index, build, [&](const detail::RootRecord& record, uint64_t remaining)
    {
        checkCancelled();
        const auto file = index.sourceRoot / fs::u8path(record.relativePath);
        auto source = decoder.read(file);
        Prepare prepare(*context, file.parent_path());
        const auto sphere = prepare.sphere(source.node->getBound());
        source.node->accept(prepare);
        if (!prepare.geometries) throw std::runtime_error("OSGB overview root has no resident geometry");
        detail::OverviewSettings overviewSettings;
        overviewSettings.triangleRatio = settings.triangleRatio;
        overviewSettings.maximumError = settings.maximumSimplificationError;
        overviewSettings.textureMaxDimension = settings.textureMaxDimension;
        overviewSettings.maxResidentBytes = remaining;
        overviewSettings.colorTexturesSRGB = settings.colorTexturesSRGB;
        overviewSettings.cancelled = cancelled;
        auto reduced = detail::reduceResident(*source.node, overviewSettings);
        checkCancelled();
        detail::PreparedRoot prepared;
        prepared.bound = vsg::dsphere(sphere.x, sphere.y, sphere.z, sphere.w);
        prepared.full = osg2vsg::convert(*source.node, options);
        prepared.overview = osg2vsg::convert(*reduced.node, options);
        if (!prepared.full || !prepared.overview) throw std::runtime_error("OSGB overview conversion failed");
        annotateRanges(*prepared.full, prepare);
        const auto colors = settings.colorTexturesSRGB ? detail::OSGBColorSpace::SRGB : detail::OSGBColorSpace::Linear;
        resources.process(*prepared.full, colors);
        resources.process(*prepared.overview, colors);
        prepared.workingBytes = reduced.stats.estimatedPeakBytes;
        prepared.inputTriangles = reduced.stats.inputTriangles;
        prepared.outputTriangles = reduced.stats.outputTriangles;
        for (const auto& image : source.externalImages)
        {
            if (!detail::pathWithin(image, index.sourceRoot)) throw std::runtime_error("OSGB overview image escapes source root");
            detail::SourceDependency dependency;
            dependency.relativePath = fs::relative(image, index.sourceRoot).generic_u8string();
            dependency.identity.size = fs::file_size(image);
            dependency.identity.modifiedTicks = fs::last_write_time(image).time_since_epoch().count();
            dependency.identity.sha256 = source.externalImageHashes.at(image);
            if (detail::sha256File(image) != dependency.identity.sha256) throw std::runtime_error("OSGB overview image changed during conversion");
            prepared.sourceDependencies.push_back(std::move(dependency));
        }
        if (detail::sha256File(file) != source.sourceSha256) throw std::runtime_error("OSGB overview source changed during conversion");
        if (settings.progress) settings.progress(++completed, static_cast<uint32_t>(index.roots.size()));
        return prepared;
    }, cancelled);
    BuildResult answer;
    answer.manifestPath = result.manifestPath.u8string(); answer.roots = result.roots;
    answer.regions = result.regions; answer.outputBytes = result.outputBytes;
    answer.inputTriangles = result.inputTriangles; answer.outputTriangles = result.outputTriangles; answer.reused = result.reused;
    return answer;
}

vsg::ref_ptr<vsg::Object> OSGB::read(const vsg::Path& filename, vsg::ref_ptr<const vsg::Options> options) const
{
    auto path = fs::u8path(filename.string());
    auto extension = vsg::lowerCaseFileExtension(filename);
    if (_impl->closed) return {};
    std::optional<Impl::Attempt> attempt;
    try
    {
        if (extension != ".osgb" && extension != ".shp" && extension != ".osgbset" && extension != ".osgbregion" && path.filename() != "metadata.xml" && !fs::is_directory(path)) return {};
        attempt.emplace(*_impl);
        if (!path.is_absolute())
        {
            auto found = vsg::findFile(filename, options);
            if (!found.empty()) path = fs::u8path(found.string());
        }
        path = fs::absolute(path).lexically_normal();
        auto local = options ? vsg::Options::create(*options) : vsg::Options::create();
        // 子请求始终回到这个图层的 Reader，关闭不会影响其他图层。
        local->readerWriters = {vsg::ref_ptr<OSGB>(const_cast<OSGB*>(this)), vsg::glsl::create(), vsg::spirv::create(), vsg::VSG::create()};
        // 不跨图层复用节点或读取结果，确保关闭语义与坐标上下文隔离。
        local->sharedObjects = {};
        local->setObject(osgbPagingTokenKey, _impl->token);
        auto inherited = options ? options->getObject<Context>(contextKey) : nullptr;
        vsg::ref_ptr<const Context> context(inherited);
        const bool dataset = fs::is_directory(path) || path.filename() == "metadata.xml" || extension == ".shp";
        std::optional<detail::DatasetManifest> manifest;
        if (extension == ".osgbset" || (extension == ".osgbregion" && !context))
        {
            const auto entry = extension == ".osgbset" ? path : path.parent_path() / "dataset.osgbset";
            manifest = detail::inspectManifest(entry, conversionIdentity(_impl->settings.colorTexturesSRGB), fs::u8path(_impl->settings.sourceRoot));
        }
        std::optional<detail::DatasetIndex> index;
        fs::path indexCache;
        if (dataset)
        {
            detail::IndexReadOptions indexOptions;
            indexOptions.sourceRootOverride = fs::u8path(_impl->settings.sourceRoot);
            if (!_impl->settings.cacheDirectory.empty())
            {
                indexCache = fs::u8path(_impl->settings.cacheDirectory) / detail::sha256Bytes(path.generic_u8string()) / "index";
                indexOptions.cacheDirectory = indexCache;
            }
            index = detail::readDatasetIndex(path, indexOptions);
        }
        if (!context)
        {
            fs::path metadata;
            if (manifest) metadata = manifest->sourceRoot / "metadata.xml";
            else if (dataset) metadata = index->sourceRoot / "metadata.xml";
            else
            {
                auto parent = path.parent_path();
                for (int depth = 0; depth < 4 && !parent.empty(); ++depth, parent = parent.parent_path())
                    if (fs::is_regular_file(parent / "metadata.xml")) { metadata = parent / "metadata.xml"; break; }
            }
            auto created = readContext(metadata);
            if (created->sourceRoot.empty()) created->sourceRoot = path.parent_path();
            context = created;
            local->setObject(contextKey, vsg::ref_ptr<Context>(const_cast<Context*>(context.get())));
        }
        local->paths.insert(local->paths.begin(), path.parent_path().u8string());
        vsg::ref_ptr<vsg::Node> node;
        uint32_t rootCount = 1;
        if (extension == ".osgbset")
        {
            node = detail::loadOverviewGroup(*manifest, local);
            rootCount = static_cast<uint32_t>(manifest->statistics.roots);
            _impl->process(*node);
        }
        else if (extension == ".osgbregion")
        {
            node = detail::readRegion(path, conversionIdentity(_impl->settings.colorTexturesSRGB), context->sourceRoot, local);
            adjustOSGBLOD(*node, _impl->token->viewportHeight.load(), _impl->token->projectionY.load());
            _impl->process(*node);
        }
        else if (dataset)
        {
            auto group = vsg::Group::create();
            Prepare prepare(*context, index->sourceRoot);
            uint64_t cached = 0;
            bool changed = false;
            index->ecefAnchor = std::array<double,3>{context->anchor.x, context->anchor.y, context->anchor.z};
            for (auto& record : index->roots)
            {
                if (_impl->closed) return {};
                const auto root = index->sourceRoot / fs::u8path(record.relativePath);
                auto page = OSGBRoot::create();
                vsg::dvec4 bound;
                if (record.localEcefSphere)
                {
                    const auto& b = *record.localEcefSphere;
                    bound = vsg::dvec4(b[0], b[1], b[2], b[3]);
                    ++cached;
                }
                else if (record.footprint)
                {
                    // 平面范围仅用于初始定位和排序，缺少 Z 时禁止据此做三维剔除。
                    const auto& xy = record.footprint->xy;
                    auto& projection = threadProjection(context->srs);
                    const auto center = projection.apply({(xy[0]+xy[2])*0.5, (xy[1]+xy[3])*0.5, context->origin.z}) - context->anchor;
                    double radius = 1.0;
                    for (int x : {0,2}) for (int y : {1,3})
                        radius = std::max(radius, vsg::length(projection.apply({xy[x],xy[y],context->origin.z}) - context->anchor - center));
                    bound = vsg::dvec4(center.x,center.y,center.z,radius);
                    page->boundTrusted = false;
                }
                else
                {
                    record.source.sha256 = detail::sha256File(root);
                    auto source = _impl->timed(&Performance::decodeMilliseconds, [&] { return _impl->decoder->read(root).node; });
                    bound = prepare.sphere(source->getBound());
                    record.localEcefSphere = std::array<double,4>{bound.x,bound.y,bound.z,bound.w};
                    changed = true;
                }
                page->bound = vsg::dsphere(bound.x,bound.y,bound.z,bound.w);
                page->filename = root.u8string(); page->options = local;
                page->children[0].minimumScreenHeightRatio = 0.0;
                group->addChild(page);
            }
            if (changed && !indexCache.empty())
            {
                try { detail::publishDatasetIndex(*index, indexCache); }
                catch (const std::exception& e) { std::lock_guard<std::mutex> lock(_impl->mutex); _impl->performance.cacheDiagnostic = e.what(); }
            }
            rootCount = static_cast<uint32_t>(index->roots.size());
            {
                std::lock_guard<std::mutex> lock(_impl->mutex);
                _impl->performance.indexedRoots = rootCount; _impl->performance.cachedBounds = cached;
            }
            node = group;
        }
        else
        {
            const auto cache = _impl->cache(*context);
            if (cache)
            {
                const auto cached = _impl->timed(&Performance::cacheLoadMilliseconds, [&] { return cache->load(path, local); });
                node = cached.node;
                std::lock_guard<std::mutex> lock(_impl->mutex);
                if (node) ++_impl->performance.cacheHits; else ++_impl->performance.cacheMisses;
                if (!cached.diagnostic.empty()) _impl->performance.cacheDiagnostic = cached.diagnostic;
            }
            if (!node)
            {
                auto decoded = _impl->timed(&Performance::decodeMilliseconds, [&] { return _impl->decoder->read(path); });
                auto source = decoded.node;
                Prepare prepare(*context, path.parent_path());
                _impl->timed(&Performance::prepareMilliseconds, [&] {
                    source->accept(prepare);
                    if (!prepare.geometries) throw std::runtime_error("OSGB root contains no resident geometry");
                    return true;
                });
                node = _impl->timed(&Performance::convertMilliseconds, [&] { return osg2vsg::convert(*source, local); });
                if (!node) throw std::runtime_error("OSGB converter produced no VSG node");
                annotateRanges(*node, prepare);
                _impl->process(*node);
                if (cache && !_impl->closed)
                {
                    detail::ArtifactPage page;
                    page.sourceFile = path; page.externalImages = decoded.externalImages;
                    page.expectedSourceSha256 = decoded.sourceSha256; page.expectedImageHashes = decoded.externalImageHashes;
                    const auto stored = _impl->timed(&Performance::cacheStoreMilliseconds, [&] { return cache->store(page, *node); });
                    std::lock_guard<std::mutex> lock(_impl->mutex);
                    if (stored.state == detail::OSGBArtifacts::StoreState::Stored) ++_impl->performance.cacheWrites;
                    if (!stored.diagnostic.empty()) _impl->performance.cacheDiagnostic = stored.diagnostic;
                }
            }
            else _impl->process(*node);
            adjustOSGBLOD(*node, _impl->token->viewportHeight.load(), _impl->token->projectionY.load());
        }
        // 缓存与显式派生页均恢复源范围语义；运行时节点仍可按普通 PagedLOD 序列化。
        node = detail::installOSGBLOD(node);
        if (!inherited)
        {
            const auto initial = status(node);
            {
                std::lock_guard<std::mutex> lock(_impl->mutex);
                _impl->initialDraws = initial.residentDraws; _impl->initialPages = initial.pagedNodes;
            }
            vsg::ComputeBounds bounds;
            node->accept(bounds);
            if (!bounds.bounds.valid()) throw std::runtime_error("OSGB converted bounds are invalid");
            auto center = (bounds.bounds.min + bounds.bounds.max) * 0.5;
            const double radius = vsg::length(bounds.bounds.max - bounds.bounds.min) * 0.5;
            if (!context->srs.empty())
            {
                auto wrapper = vsg::MatrixTransform::create(vsg::translate(context->anchor));
                wrapper->addChild(node); node = wrapper;
                auto ellipsoid = vsg::EllipsoidModel::create();
                auto geo = ellipsoid->convertECEFToLatLongAltitude(context->anchor + center);
                node->setValue("osgb.longitude", geo.y); node->setValue("osgb.latitude", geo.x); node->setValue("osgb.altitude", geo.z);
            }
            node->setValue("osgb.georeferenced", !context->srs.empty());
            node->setValue("osgb.rootCount", rootCount);
            node->setValue("osgb.range", std::max(100.0, radius * 3.0));
        }
        if (_impl->closed) return {};
        attempt->finish(true);
        return node;
    }
    catch (const std::exception& failure)
    {
        if (attempt) attempt->finish(false);
        std::lock_guard<std::mutex> lock(_impl->mutex);
        _impl->error = path.u8string() + ": " + failure.what();
        vsg::warn(_impl->error);
        return {};
    }
}
