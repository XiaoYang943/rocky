#include "../globe/catch.hpp"
#include "OSGBStats.h"
#include <geostudio/native/render/import/osgb/OSGB.h>
#include <osg/Geode>
#include <osg/Geometry>
#include <osg/MatrixTransform>
#include <osg/PagedLOD>
#include <osg/ProxyNode>
#include <osg/Texture2D>
#include <osgDB/Registry>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <chrono>
#include <thread>

using geostudio::native::io::OSGB;
namespace fs = std::filesystem;
namespace
{
// 测试拥有固定 build 子目录，仅创建缺失内容；已有内容不一致时失败。
void fixture(const fs::path& path, const std::string& bytes)
{
    fs::create_directories(path.parent_path());
    if (fs::exists(path))
    {
        std::ifstream input(path, std::ios::binary);
        REQUIRE(std::string(std::istreambuf_iterator<char>(input), {}) == bytes);
    }
    else
    {
        std::ofstream output(path, std::ios::binary);
        output.write(bytes.data(), bytes.size());
        REQUIRE(output.good());
    }
}
osg::ref_ptr<osg::Node> triangle()
{
    auto geometry = new osg::Geometry;
    auto vertices = new osg::Vec3Array;
    vertices->push_back({0,0,0}); vertices->push_back({10,0,0}); vertices->push_back({0,10,0});
    geometry->setVertexArray(vertices);
    auto uv = new osg::Vec2Array;
    uv->push_back({0,0}); uv->push_back({1,0}); uv->push_back({0,1});
    geometry->setTexCoordArray(0, uv);
    geometry->addPrimitiveSet(new osg::DrawArrays(GL_TRIANGLES, 0, 3));
    auto image = new osg::Image;
    image->allocateImage(2, 2, 1, GL_RGBA, GL_UNSIGNED_BYTE);
    std::fill(image->data(), image->data() + 16, 255);
    geometry->getOrCreateStateSet()->setTextureAttributeAndModes(0, new osg::Texture2D(image));
    auto geode = new osg::Geode;
    geode->addDrawable(geometry);
    return geode;
}
void writeNode(const fs::path& path, const osg::Node& node)
{
    auto writer = osgDB::Registry::instance()->getReaderWriterForExtension("osgb");
    REQUIRE(writer);
    auto options = new osgDB::Options("WriteImageHint=IncludeData");
    osg::ref_ptr<osgDB::Options> owner = options;
    options->setPluginStringData("fileType", "Binary");
    std::ostringstream output(std::ios::binary);
    REQUIRE(writer->writeNode(node, output, options).success());
    fixture(path, output.str());
}
osg::ref_ptr<osg::PagedLOD> page(const std::string& child)
{
    auto result = new osg::PagedLOD;
    result->setRangeMode(osg::LOD::PIXEL_SIZE_ON_SCREEN);
    result->addChild(triangle(), 0, 256);
    result->setFileName(1, child); result->setRange(1, 256, FLT_MAX);
    return result;
}
fs::path samples()
{
    const auto root = fs::u8path(GN_OSGB_FIXTURES) / fs::u8path(u8"中文 空格");
    writeNode(root / "leaf.osgb", *triangle());
    writeNode(root / "root.osgb", *page("leaf.osgb"));
    writeNode(root / "missing.osgb", *page("absent.osgb"));
    auto bad = page("leaf.osgb"); bad->setRange(1, 300, FLT_MAX);
    writeNode(root / "bad.osgb", *bad);
    auto distance = page("leaf.osgb");
    distance->setRangeMode(osg::LOD::DISTANCE_FROM_EYE_POINT);
    distance->setRange(0, 100, FLT_MAX); distance->setRange(1, 0, 100);
    writeNode(root / "distance.osgb", *distance);
    auto finite = page("leaf.osgb"); finite->setRange(1, 256, 512);
    writeNode(root / "finite.osgb", *finite);
    auto sentinel = page("leaf.osgb"); sentinel->setRange(1, 256, 1e30f);
    writeNode(root / "sentinel.osgb", *sentinel);
    auto translated = new osg::MatrixTransform(osg::Matrixd::translate(20,30,40));
    osg::ref_ptr<osg::MatrixTransform> translatedOwner = translated;
    translated->addChild(triangle());
    writeNode(root / "translated.osgb", *translated);
    auto proxy = new osg::ProxyNode;
    osg::ref_ptr<osg::ProxyNode> proxyOwner = proxy;
    proxy->setFileName(0, "leaf.osgb");
    writeNode(root / "proxy.osgb", *proxy);
    fixture(root / "corrupt.osgb", "not an OSGB stream");
    const auto dataset = root / "dataset";
    fixture(dataset / "metadata.xml", "<ModelMetadata><SRS>EPSG:4545</SRS><SRSOrigin>607429,3938520,0</SRSOrigin></ModelMetadata>");
    writeNode(dataset / "Data" / "Tile_A" / "Tile_A.osgb", *page("child.osgb"));
    writeNode(dataset / "Data" / "Tile_A" / "child.osgb", *triangle());
    writeNode(dataset / "Data" / "Tile_B" / "Tile_B.osgb", *page("child.osgb"));
    writeNode(dataset / "Data" / "Tile_B" / "child.osgb", *triangle());
    return root;
}

// 使用真实 DatabasePager 读取线程及 Merge 队列；空编译 Context 不分配 GPU 资源。
// 请求显式模拟可见性选择，视锥选择、Vulkan Compile 和像素仍由 Demo 验收。
struct CpuPager
{
    vsg::ref_ptr<vsg::Viewer> viewer = vsg::Viewer::create();
    vsg::ref_ptr<vsg::DatabasePager> pager = vsg::DatabasePager::create();
    vsg::ref_ptr<vsg::FrameStamp> frame = vsg::FrameStamp::create();
    CpuPager()
    {
        pager->compileManager = vsg::CompileManager::create(*viewer, vsg::ref_ptr<vsg::ResourceHints>{});
        frame->frameCount = 10;
        pager->frameCount = frame->frameCount;
        pager->start(1);
    }
    void request(const vsg::PagedLOD* source)
    {
        auto node = const_cast<vsg::PagedLOD*>(source);
        node->frameHighResLastUsed = frame->frameCount;
        node->requestCount = 1;
        node->priority = 1.0;
        pager->request(vsg::ref_ptr<vsg::PagedLOD>(node));
    }
    bool drain()
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        do
        {
            vsg::CompileResult result;
            pager->updateSceneGraph(frame, result);
            if (pager->numActiveRequests == 0) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        } while (std::chrono::steady_clock::now() < deadline);
        return false;
    }
};
}

TEST_CASE("Native OSGB preserves geometry, textures and lazy relative pages", "[osgb]")
{
    const auto root = samples();
    auto reader = OSGB::create();
    REQUIRE(OSGB::available());
    auto model = reader->read((root / "root.osgb").u8string()).cast<vsg::Node>();
    INFO(reader->error());
    REQUIRE(model);
    OSGBStats coarse; model->accept(coarse);
    CHECK(coarse.draws == 1); CHECK(coarse.textures == 1);
    REQUIRE(coarse.pages.size() == 1);
    auto paged = coarse.pages.front();
    CHECK_FALSE(paged->children[0].node);
    REQUIRE(paged->children[1].node);
    CHECK(paged->children[0].minimumScreenHeightRatio == Approx(256.0 / 1080.0));
    CHECK(fs::u8path(paged->filename.string()).is_absolute());
    auto child = vsg::read_cast<vsg::Node>(paged->filename, paged->options);
    REQUIRE(child);
    OSGBStats fine; child->accept(fine);
    CHECK(fine.draws == 1); CHECK(fine.textures == 1); CHECK(fine.pages.empty());
    auto compiler = vsg::ShaderCompiler::create();
    REQUIRE(compiler->supported()); REQUIRE_FALSE(fine.pipelines.empty());
    for (auto pipeline : fine.pipelines) CHECK(compiler->compile(pipeline->stages));
    vsg::ComputeBounds bounds; bounds.useNodeBounds = false; child->accept(bounds);
    CHECK(bounds.bounds.min.x == Approx(0)); CHECK(bounds.bounds.max.x == Approx(10));
    reader->close(); reader->close();
    CHECK_FALSE(vsg::read(paged->filename, paged->options));
    CHECK(OSGB::create()->read((root / "leaf.osgb").u8string()));
    auto translated = OSGB::create()->read((root / "translated.osgb").u8string());
    REQUIRE(translated);
    vsg::ComputeBounds translatedBounds; translatedBounds.useNodeBounds = false; translated->accept(translatedBounds);
    CHECK(translatedBounds.bounds.min.x == Approx(20)); CHECK(translatedBounds.bounds.max.x == Approx(30));
    CHECK(translatedBounds.bounds.min.y == Approx(30)); CHECK(translatedBounds.bounds.min.z == Approx(40));
}

TEST_CASE("Native OSGB fails explicitly without discarding resident fallback", "[osgb]")
{
    const auto root = samples();
    auto reader = OSGB::create();
    CHECK_FALSE(reader->read((root / "bad.osgb").u8string()));
    CHECK(reader->error().find("discontinuous") != std::string::npos);
    CHECK_FALSE(reader->read((root / "finite.osgb").u8string()));
    CHECK(reader->error().find("unbounded") != std::string::npos);
    REQUIRE(reader->read((root / "sentinel.osgb").u8string()));
    CHECK_FALSE(reader->read((root / "corrupt.osgb").u8string()));
    CHECK_FALSE(reader->read((root / "proxy.osgb").u8string()));
    CHECK(reader->error().find("ProxyNode") != std::string::npos);
    auto model = reader->read((root / "missing.osgb").u8string());
    REQUIRE(model);
    OSGBStats stats; model->accept(stats);
    REQUIRE(stats.pages.size() == 1);
    CHECK_FALSE(vsg::read(stats.pages[0]->filename, stats.pages[0]->options));
    CHECK(reader->error().find("absent.osgb") != std::string::npos);
    CHECK(stats.pages[0]->children[1].node);
    auto distance = reader->read((root / "distance.osgb").u8string());
    REQUIRE(distance);
    OSGBStats distanceStats; distance->accept(distanceStats);
    REQUIRE(distanceStats.pages.size() == 1);
    CHECK(distanceStats.pages[0]->children[1].node);
    // 距离阈值遵循当前 VSG 投影尺度，替代转换器固定角度的近似值。
    CHECK(distanceStats.pages[0]->children[0].minimumScreenHeightRatio == Approx(std::sqrt(50.0) * std::sqrt(3.0) / (std::sqrt(2.0) * 100.0)));
}

TEST_CASE("OSGB metadata supplies ECEF placement and shared paging context", "[osgb]")
{
    const auto root = samples() / "dataset";
    auto reader = OSGB::create();
    auto model = reader->read(root.u8string()).cast<vsg::MatrixTransform>();
    INFO(reader->error());
    REQUIRE(model);
    CHECK(vsg::length(vsg::dvec3(model->matrix[3].x, model->matrix[3].y, model->matrix[3].z)) > 6300000);
    // 独立 gdaltransform EPSG:4545 → EPSG:4978 的固定控制值，容差为毫米。
    CHECK(std::abs(model->matrix[3].x - (-1706813.1367337)) < 0.001);
    CHECK(std::abs(model->matrix[3].y - 4905416.01568093) < 0.001);
    CHECK(std::abs(model->matrix[3].z - 3689521.96541088) < 0.001);
    bool georeferenced = false; model->getValue("osgb.georeferenced", georeferenced);
    CHECK(georeferenced);
    double longitude = 0, latitude = 0;
    REQUIRE(model->getValue("osgb.longitude", longitude));
    REQUIRE(model->getValue("osgb.latitude", latitude));
    CHECK(longitude > 108); CHECK(longitude < 110);
    CHECK(latitude > 35); CHECK(latitude < 36);
    OSGBStats roots; model->accept(roots);
    CHECK(roots.draws == 0); REQUIRE(roots.pages.size() == 2);
    auto coarse = vsg::read_cast<vsg::Node>(roots.pages[0]->filename, roots.pages[0]->options);
    REQUIRE(coarse);
    OSGBStats stats; coarse->accept(stats);
    REQUIRE(stats.pages.size() == 1); CHECK(stats.draws == 1);
    auto fine = vsg::read_cast<vsg::Node>(stats.pages[0]->filename, stats.pages[0]->options);
    REQUIRE(fine);
    vsg::ComputeBounds bounds; fine->accept(bounds);
    CHECK(vsg::length(bounds.bounds.max - bounds.bounds.min) < 30);
    CHECK(vsg::length(bounds.bounds.min) < 20);
    auto world = reader->read((root / "Data/Tile_A/child.osgb").u8string()).cast<vsg::MatrixTransform>();
    REQUIRE(world); CHECK(world->matrix == model->matrix);
}

TEST_CASE("OSGB multi-root pages read and merge through the background pager", "[osgb][paging]")
{
    auto reader = OSGB::create();
    auto model = reader->read((samples() / "dataset").u8string()).cast<vsg::Node>();
    REQUIRE(model);
    CHECK(reader->status(model).residentDraws == 0);
    CpuPager queue;
    OSGBStats roots; model->accept(roots);
    REQUIRE(roots.pages.size() == 2);
    for (auto root : roots.pages) queue.request(root);
    REQUIRE(queue.drain());
    auto first = reader->status(model, queue.frame->frameCount);
    CHECK(first.attachedPages == 2);
    CHECK(first.residentDraws == 2);
    CHECK(first.readCompleted == 3);
    OSGBStats coarse; model->accept(coarse);
    REQUIRE(coarse.pages.size() == 4);
    for (auto page : coarse.pages) if (!page->children[0].node) queue.request(page);
    REQUIRE(queue.drain());
    auto fine = reader->status(model, queue.frame->frameCount);
    CHECK(fine.attachedPages == 4);
    CHECK(fine.readCompleted == 5);
    CHECK(fine.readFailures == 0);
    CHECK(fine.reading == 0);
    CHECK(queue.pager->compileManager->successfulCompileCount == 4);
    reader->close();
    CHECK(reader->status().closed);
}

TEST_CASE("OSGB pager failures preserve coarse content and closed readers reject late requests", "[osgb][paging]")
{
    const auto root = samples();
    auto reader = OSGB::create();
    auto model = reader->read((root / "missing.osgb").u8string()).cast<vsg::Node>();
    REQUIRE(model);
    OSGBStats pages; model->accept(pages);
    REQUIRE(pages.pages.size() == 1);
    CpuPager queue;
    queue.request(pages.pages.front());
    REQUIRE(queue.drain());
    auto failed = reader->status(model, queue.frame->frameCount);
    CHECK(failed.readFailures == 1);
    CHECK(failed.readCompleted == 1);
    CHECK(failed.attachedPages == 0);
    CHECK(failed.residentDraws == 1);
    CHECK(failed.deferredPages == 1);
    REQUIRE(failed.deferredFiles.size() == 1);
    CHECK(failed.deferredFiles[0].find("absent.osgb") != std::string::npos);
    reader->close();
    auto reopened = OSGB::create();
    auto replacement = reopened->read((root / "root.osgb").u8string()).cast<vsg::Node>();
    REQUIRE(replacement);
    OSGBStats replacementPages; replacement->accept(replacementPages);
    REQUIRE(replacementPages.pages.size() == 1);
    reopened->close();
    queue.request(replacementPages.pages.front());
    REQUIRE(queue.drain());
    CHECK_FALSE(replacementPages.pages.front()->children[0].node);
    CHECK(reopened->status(replacement).readCompleted == 1);
    auto nextReader = OSGB::create();
    auto next = nextReader->read((root / "root.osgb").u8string()).cast<vsg::Node>();
    REQUIRE(next);
    OSGBStats nextPages; next->accept(nextPages);
    REQUIRE(nextPages.pages.size() == 1);
    queue.request(nextPages.pages.front());
    REQUIRE(queue.drain());
    CHECK(nextReader->status(next).attachedPages == 1);
    CHECK(nextReader->status().error.empty());
}

TEST_CASE("OSGB asynchronous dataset reuses validated bounds without decoding roots", "[osgb][index]")
{
    const auto root = samples();
    OSGB::Settings settings;
    settings.cacheDirectory = (root / "derived").u8string();
    auto first = OSGB::create(); first->configure(settings);
    auto pending = first->readAsync((root / "dataset").u8string());
    CHECK_THROWS(first->configure(settings));
    REQUIRE(pending.wait_for(std::chrono::seconds(15)) == std::future_status::ready);
    REQUIRE(pending.get());
    CHECK(first->performance().indexedRoots == 2);
    REQUIRE(first->performance().cacheDiagnostic.empty());
    auto second = OSGB::create(); second->configure(settings);
    auto node = second->read((root / "dataset").u8string()).cast<vsg::Node>();
    REQUIRE(node);
    CHECK(second->performance().cachedBounds == 2);
    CHECK(second->performance().decodeMilliseconds == 0.0);
    CHECK(second->status(node).residentDraws == 0);
    CHECK_THROWS(second->configure(settings));
    second->close();
    auto cancelled = second->readAsync((root / "dataset").u8string());
    REQUIRE(cancelled.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    CHECK_FALSE(cancelled.get());
}

TEST_CASE("OSGB runtime LOD uses current viewport and projection", "[osgb][lod]")
{
    const auto root = samples();
    auto reader = OSGB::create();
    auto node = reader->read((root / "root.osgb").u8string()).cast<vsg::Node>();
    REQUIRE(node);
    OSGBStats stats; node->accept(stats);
    REQUIRE(stats.pages.size() == 1);
    reader->updateView(node, 2160, 2.0);
    CHECK(stats.pages.front()->children[0].minimumScreenHeightRatio == Approx(256.0 / 2160.0));
    CHECK(stats.pages.front()->children[1].minimumScreenHeightRatio == 0.0);
    auto distanceReader = OSGB::create();
    auto distance = distanceReader->read((root / "distance.osgb").u8string()).cast<vsg::Node>();
    REQUIRE(distance);
    OSGBStats distanceStats; distance->accept(distanceStats);
    REQUIRE(distanceStats.pages.size() == 1);
    distanceReader->updateView(distance, 2160, 2.0);
    const auto page = distanceStats.pages.front();
    CHECK(page->children[0].minimumScreenHeightRatio == Approx(page->bound.r * 2.0 / (std::sqrt(2.0) * 100.0)));
}

TEST_CASE("OSGB built dataset starts with overview and retains resident region roots", "[osgb][dataset-build]")
{
    const auto root = samples();
    OSGB::BuildSettings settings;
    settings.outputDirectory = (root / "built-dataset").u8string();
    settings.rootsPerRegion = 2;
    settings.textureMaxDimension = 1;
    const auto built = OSGB::buildDataset((root / "dataset").u8string(), settings);
    REQUIRE(fs::is_regular_file(fs::u8path(built.manifestPath)));
    CHECK(fs::u8path(built.manifestPath).extension() == ".osgbset");
    CHECK(built.roots == 2);
    CHECK(built.regions == 1);
    CHECK(built.outputBytes > 0);
    CHECK(built.inputTriangles == 2);
    CHECK(built.outputTriangles == 2);
    // 固定受管目录允许上一轮有效制品命中；本轮第二次必须确认复用。
    const auto repeated = OSGB::buildDataset((root / "dataset").u8string(), settings);
    CHECK(repeated.reused);
    CHECK(repeated.manifestPath == built.manifestPath);
    CHECK(repeated.outputBytes == built.outputBytes);

    auto reader = OSGB::create();
    const auto model = reader->read(built.manifestPath).cast<vsg::Node>();
    INFO(reader->error());
    REQUIRE(model);
    OSGBStats overview;
    model->accept(overview);
    CHECK(overview.draws == 2);
    CHECK(overview.textures >= 1);
    REQUIRE(overview.pages.size() == 1);
    const auto regionPage = overview.pages.front();
    REQUIRE(regionPage->children[1].node);
    CHECK_FALSE(regionPage->children[0].node);
    CHECK(fs::u8path(regionPage->filename.string()).extension() == ".osgbregion");
    REQUIRE(regionPage->options);
    const auto region = vsg::read_cast<vsg::Node>(regionPage->filename, regionPage->options);
    INFO(reader->error());
    REQUIRE(region);
    OSGBStats resident;
    region->accept(resident);
    // 区域高精侧先提供两个完整根的驻留粗模，再延续原有 OSGB 精模分页。
    CHECK(resident.draws == 2);
    CHECK(resident.textures >= 1);
    REQUIRE(resident.pages.size() == 2);
    std::set<std::string> finePaths;
    for (const auto finePage : resident.pages)
    {
        REQUIRE(finePage->children[1].node);
        CHECK_FALSE(finePage->children[0].node);
        REQUIRE(finePage->options);
        const auto path = fs::u8path(finePage->filename.string());
        CHECK(path.is_absolute());
        CHECK(path.filename() == "child.osgb");
        finePaths.insert(path.generic_u8string());
        const auto fine = vsg::read_cast<vsg::Node>(finePage->filename, finePage->options);
        INFO(reader->error());
        REQUIRE(fine);
        OSGBStats geometry;
        fine->accept(geometry);
        CHECK(geometry.draws == 1);
        CHECK(geometry.textures == 1);
        CHECK(geometry.pages.empty());
        vsg::ComputeBounds bounds;
        bounds.useNodeBounds = false;
        fine->accept(bounds);
        REQUIRE(bounds.bounds.valid());
        CHECK(vsg::length(bounds.bounds.max - bounds.bounds.min) < 30);
        CHECK(vsg::length(bounds.bounds.min) < 20);
    }
    CHECK(finePaths.size() == 2);
}

TEST_CASE("OSGB converted page cache avoids decode and rebinds texture and paging context", "[osgb][page-cache]")
{
    const auto root = samples();
    const auto source = root / "dataset" / "Data" / "Tile_A" / "Tile_A.osgb";
    OSGB::Settings settings;
    settings.cacheDirectory = (root / "derived-pages").u8string();
    auto indexOnly = OSGB::create();
    indexOnly->configure(settings);
    REQUIRE(indexOnly->read(source.u8string()));
    CHECK(indexOnly->performance().cacheHits == 0);
    CHECK(indexOnly->performance().cacheWrites == 0);
    CHECK_THROWS(indexOnly->enablePageCache(true));
    // 在同一受管缓存内先发布另一转换身份；颜色策略属于生成器身份，反复运行复用固定两种 blob。
    // 不删除旧 fixture，也不通过新目录绕开旧转换器缓存的安全失效路径。
    auto previousSettings = settings;
    previousSettings.colorTexturesSRGB = !settings.colorTexturesSRGB;
    auto previous = OSGB::create();
    previous->configure(previousSettings);
    previous->enablePageCache(true);
    REQUIRE(previous->read(source.u8string()));
    const auto previousPerformance = previous->performance();
    INFO(previousPerformance.cacheDiagnostic);
    REQUIRE((previousPerformance.cacheDiagnostic.empty() ||
        previousPerformance.cacheDiagnostic == "OSGB page cache identity changed"));
    REQUIRE(previousPerformance.cacheWrites + previousPerformance.cacheHits == 1);
    previous->close();
    auto first = OSGB::create();
    first->configure(settings);
    first->enablePageCache(true);
    const auto initial = first->read(source.u8string()).cast<vsg::Node>();
    INFO(first->error());
    REQUIRE(initial);
    const auto firstPerformance = first->performance();
    INFO(firstPerformance.cacheDiagnostic);
    REQUIRE(firstPerformance.cacheDiagnostic == "OSGB page cache identity changed");
    CHECK(firstPerformance.cacheMisses == 1);
    CHECK(firstPerformance.cacheHits == 0);
    CHECK(firstPerformance.cacheWrites == 1);
    CHECK(firstPerformance.decodeMilliseconds > 0.0);
    first->close();

    auto second = OSGB::create();
    second->configure(settings);
    second->enablePageCache(true);
    const auto cached = second->read(source.u8string()).cast<vsg::Node>();
    INFO(second->error());
    REQUIRE(cached);
    CHECK(second->performance().cacheHits > 0);
    CHECK(second->performance().decodeMilliseconds == 0.0);
    CHECK(second->performance().cacheDiagnostic.empty());
    OSGBStats stats;
    cached->accept(stats);
    CHECK(stats.draws == 1);
    CHECK(stats.textures == 1);
    REQUIRE(stats.pages.size() == 1);
    const auto page = stats.pages.front();
    REQUIRE(page->children[1].node);
    CHECK_FALSE(page->children[0].node);
    REQUIRE(page->options);
    CHECK(fs::u8path(page->filename.string()).lexically_normal() == (source.parent_path() / "child.osgb").lexically_normal());
    second->updateView(cached, 2160, 2.0);
    CHECK(page->children[0].minimumScreenHeightRatio == Approx(256.0 / 2160.0));
    // 首个 Reader 已关闭，成功读取说明缓存分页绑定的是第二个 Reader 和当前坐标上下文。
    const auto fine = vsg::read_cast<vsg::Node>(page->filename, page->options);
    INFO(second->error());
    REQUIRE(fine);
    OSGBStats fineStats;
    fine->accept(fineStats);
    CHECK(fineStats.draws == 1);
    CHECK(fineStats.textures == 1);
    CHECK(fineStats.pages.empty());
    vsg::ComputeBounds bounds;
    bounds.useNodeBounds = false;
    fine->accept(bounds);
    REQUIRE(bounds.bounds.valid());
    CHECK(vsg::length(bounds.bounds.max - bounds.bounds.min) < 30);
    CHECK(vsg::length(bounds.bounds.min) < 20);
    second->close();
    CHECK_FALSE(vsg::read(page->filename, page->options));
}
