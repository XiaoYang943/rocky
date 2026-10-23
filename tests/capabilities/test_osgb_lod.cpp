#include "../globe/catch.hpp"
#include "../../src/render/import/osgb/OSGBLOD.h"
#include <geostudio/native/render/import/osgb/OSGBPaging.h>
#include <osg/CullStack>
#include <osg/PagedLOD>
#include <vsg/all.h>
#include <vsg/vk/State.h>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <sstream>
#include <thread>

using namespace geostudio::native::io;
namespace
{
// 实际执行固定 OSG PagedLOD::traverse；仅替代叶子遍历，完全不创建图形上下文。
struct SourceCull : osg::NodeVisitor, osg::CullStack
{
    std::string selected;
    SourceCull() : osg::NodeVisitor(TRAVERSE_ACTIVE_CHILDREN) {}
    osg::CullStack* asCullStack() override { return this; }
    const osg::CullStack* asCullStack() const override { return this; }
    float getDistanceToViewPoint(const osg::Vec3& p, bool scaled) const override
    { return (p - getEyeLocal()).length() * (scaled ? getLODScale() : 1.0f); }
    void apply(osg::Node& node) override { selected = node.getName(); }
};
struct Leaf : vsg::Inherit<vsg::Node, Leaf>
{
    std::string name;
    std::string* selected;
    Leaf(std::string value, std::string& output) : name(std::move(value)), selected(&output) {}
    void accept(vsg::RecordTraversal&) const override { *selected = name; }
};
vsg::dmat4 toVSG(const osg::Matrixd& matrix, bool projection)
{
    vsg::dmat4 result;
    for (unsigned c = 0; c < 4; ++c) for (unsigned r = 0; r < 4; ++r)
        result[c][r] = matrix(c, r) * (projection && r == 1 ? -1.0 : 1.0);
    // 转为 Vulkan 反向深度，保证剔除路径也使用真实坐标约定。
    if (projection) for (unsigned c = 0; c < 4; ++c) result[c][2] = (matrix(c, 3) - matrix(c, 2)) * 0.5;
    return result;
}
std::string osgChoice(bool pixels, double split, const osg::Matrixd& projection, const osg::Matrixd& modelview,
    unsigned width, unsigned height, const osg::Vec3& center, double radius)
{
    osg::ref_ptr<osg::PagedLOD> page = new osg::PagedLOD;
    page->setCenterMode(osg::LOD::USER_DEFINED_CENTER);
    page->setCenter(center); page->setRadius(radius);
    page->setRangeMode(pixels ? osg::LOD::PIXEL_SIZE_ON_SCREEN : osg::LOD::DISTANCE_FROM_EYE_POINT);
    auto coarse = new osg::Node; coarse->setName("coarse");
    auto fine = new osg::Node; fine->setName("fine");
    page->addChild(coarse, pixels ? 0.0f : float(split), pixels ? float(split) : 1e30f);
    page->addChild(fine, pixels ? float(split) : 0.0f, pixels ? 1e30f : float(split));
    SourceCull cull;
    cull.pushViewport(new osg::Viewport(0, 0, width, height));
    cull.pushProjectionMatrix(new osg::RefMatrix(projection));
    cull.pushModelViewMatrix(new osg::RefMatrix(modelview), osg::Transform::ABSOLUTE_RF);
    page->traverse(cull);
    return cull.selected;
}
std::string vsgChoice(bool pixels, double split, const osg::Matrixd& projection, const osg::Matrixd& modelview,
    unsigned width, unsigned height, const osg::Vec3& center, double radius, double cullRadius = 10000.0)
{
    std::string selected;
    auto page = vsg::PagedLOD::create();
    page->bound = vsg::dsphere(double(center.x()), double(center.y()), double(center.z()), cullRadius);
    page->setValue(pixels ? "osgb.pixelSplit" : "osgb.distanceSplit", split);
    page->setValue("osgb.memoryLodScale", 16.0);
    page->setValue(pixels ? "osgb.lodSphere" : "osgb.distanceSphere", vsg::dvec4(center.x(), center.y(), center.z(), radius));
    // 故意留下错误的旧阈值；专用 Record 必须独立于 screenHeightRatio。
    page->children[0].minimumScreenHeightRatio = 1000000.0;
    page->children[0].node = Leaf::create("fine", selected);
    page->children[1].node = Leaf::create("coarse", selected);
    auto node = detail::installOSGBLOD(page);
    auto traversal = vsg::RecordTraversal::create();
    traversal->frameStamp = vsg::FrameStamp::create();
    traversal->state->setProjectionAndViewMatrix(toVSG(projection, true), toVSG(modelview, false));
    auto view = vsg::View::create();
    view->camera = vsg::Camera::create();
    view->camera->viewportState = vsg::ViewportState::create(0, 0, width, height);
    traversal->viewDependentState = vsg::ViewDependentState::create(view.get());
    node->accept(*traversal);
    return selected;
}

// 读取门闩只用于确定取消时序；Reader 与 Pager 使用真实后台线程，无窗口/Device/GPU。
struct RetryReadState
{
    std::mutex mutex;
    std::condition_variable changed;
    unsigned reads = 0;
    bool blocked = false, released = false;
    std::string selected;
};
struct RetryReader : vsg::Inherit<vsg::ReaderWriter, RetryReader>
{
    std::shared_ptr<RetryReadState> state;
    explicit RetryReader(std::shared_ptr<RetryReadState> value) : state(std::move(value)) {}
    vsg::ref_ptr<vsg::Object> read(const vsg::Path&, vsg::ref_ptr<const vsg::Options>) const override
    {
        std::unique_lock lock(state->mutex);
        ++state->reads;
        state->changed.notify_all();
        state->changed.wait(lock, [&] { return !state->blocked || state->released; });
        return Leaf::create("fine", state->selected);
    }
};
struct RetryFixture
{
    std::shared_ptr<RetryReadState> reads = std::make_shared<RetryReadState>();
    vsg::ref_ptr<vsg::Viewer> viewer = vsg::Viewer::create();
    vsg::ref_ptr<OSGBDatabasePager> pager = OSGBDatabasePager::create();
    vsg::ref_ptr<OSGBPagingToken> token = OSGBPagingToken::create();
    vsg::ref_ptr<vsg::PagedLOD> page;
    vsg::ref_ptr<vsg::View> view = vsg::View::create();
    vsg::ref_ptr<vsg::RecordTraversal> record = vsg::RecordTraversal::create();
    uint64_t frame = 10;
    RetryFixture()
    {
        pager->compileManager = vsg::CompileManager::create(*viewer, vsg::ref_ptr<vsg::ResourceHints>{});
        pager->frameCount = frame;
        auto source = vsg::PagedLOD::create();
        source->bound = vsg::dsphere(0.0,0.0,0.0,100.0);
        source->filename = "retry.osgb";
        source->setValue("osgb.pixelSplit", 200.0);
        source->setValue("osgb.lodSphere", vsg::dvec4(0,0,0,100));
        source->options = vsg::Options::create();
        source->options->readerWriters = {RetryReader::create(reads)};
        source->options->setObject(osgbPagingTokenKey, token);
        source->children[1].node = Leaf::create("coarse", reads->selected);
        page = detail::installOSGBLOD(source).cast<vsg::PagedLOD>();
        view->camera = vsg::Camera::create();
        view->camera->viewportState = vsg::ViewportState::create(0,0,1920,1080);
        record->viewDependentState = view->viewDependentState;
        record->setDatabasePager(pager);
    }
    ~RetryFixture()
    {
        releaseRead();
        pager->shutdown();
    }
    // 下列 Record/Update 仅测试主线程调用，后台通过门闩发布读取完成。
    void draw(bool visible = true, double distance = 1000.0)
    {
        auto stamp = vsg::FrameStamp::create();
        stamp->frameCount = frame;
        record->setFrameStamp(stamp);
        record->state->setProjectionAndViewMatrix(
            toVSG(osg::Matrixd::perspective(60,16.0/9.0,1,10000),true),
            toVSG(osg::Matrixd::translate(visible ? 0.0 : 100000.0,0.0,-distance),false));
        page->accept(*record);
    }
    void update()
    {
        auto stamp = vsg::FrameStamp::create();
        stamp->frameCount = frame;
        vsg::CompileResult result;
        pager->updateSceneGraph(stamp, result);
    }
    unsigned readCount()
    {
        std::lock_guard lock(reads->mutex);
        return reads->reads;
    }
    void releaseRead()
    {
        std::lock_guard lock(reads->mutex);
        reads->released = true;
        reads->changed.notify_all();
    }
};
bool retryWait(const std::function<bool()>& ready)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!ready() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    return ready();
}
}

TEST_CASE("OSGB source LOD matches actual OSG traversal across views", "[osgb][lod]")
{
    for (const auto viewport : {std::pair<unsigned,unsigned>{1920,1080}, {1080,1920}, {960,540}})
    for (double fov : {30.0, 60.0, 100.0})
    for (bool ortho : {false, true})
    for (bool asymmetric : {false, true})
    {
        auto projection = ortho ? osg::Matrixd::ortho(-800, 1200, -500, 700, 1, 10000) :
            osg::Matrixd::perspective(fov, double(viewport.first)/viewport.second, 1.0, 10000.0);
        if (asymmetric && !ortho) { projection(2,0) = 0.3; projection(2,1) = -0.2; }
        const auto modelview = osg::Matrixd::rotate(0.15, osg::Vec3d(0,0,1)) * osg::Matrixd::translate(0,0,-1000);
        const osg::Vec3 center(400, 50, 0);
        osg::CullingSet culling;
        osg::ref_ptr<osg::Viewport> sourceViewport = new osg::Viewport(0,0,viewport.first,viewport.second);
        culling.setPixelSizeVector(osg::CullingSet::computePixelSizeVector(*sourceViewport, projection, modelview));
        const double actual = culling.clampedPixelSize(center, 100.0f);
        REQUIRE(detail::osgbRangeValue(vsg::dsphere(400.0,50.0,0.0,100.0), true, toVSG(projection,true), toVSG(modelview,false),
            viewport.first, viewport.second) == Approx(actual).epsilon(1e-6));
        for (double factor : {0.75, 1.0, 1.25})
        {
            const double split = actual * factor;
            REQUIRE(vsgChoice(true, split, projection, modelview, viewport.first, viewport.second, center, 100) ==
                osgChoice(true, split, projection, modelview, viewport.first, viewport.second, center, 100));
        }
    }
}

TEST_CASE("OSGB distance LOD preserves off axis and local scale semantics", "[osgb][lod]")
{
    const auto projection = osg::Matrixd::perspective(60, 1.5, 1, 10000);
    for (const auto center : {osg::Vec3(0,0,0), osg::Vec3(800,0,0)})
    for (const auto scale : {osg::Vec3d(1,1,1), osg::Vec3d(2,3,0.5)})
    for (const double split : {999.0, 1000.0, 1100.0, 2000.0, 2200.0})
    {
        const auto modelview = osg::Matrixd::scale(scale) * osg::Matrixd::translate(0,0,-1000);
        REQUIRE(vsgChoice(false, split, projection, modelview, 1200,800,center,100) ==
            osgChoice(false, split, projection, modelview, 1200,800,center,100));
    }
}

TEST_CASE("OSGB source bounds ignore conservative culling padding and memory metadata", "[osgb][lod]")
{
    const auto projection = osg::Matrixd::perspective(60, 16.0/9.0, 1,10000);
    const auto modelview = osg::Matrixd::translate(0,0,-1000);
    for (double split : {200.0,300.0})
    {
        const auto expected = osgChoice(true, split, projection, modelview, 1920,1080,{},100);
        for (double cullRadius : {100.0, 176.0, 10000.0})
            REQUIRE(vsgChoice(true,split,projection,modelview,1920,1080,{},100,cullRadius) == expected);
    }
}

TEST_CASE("OSGB runtime source LOD serializes as ordinary page and reinstalls", "[osgb][lod]")
{
    auto page = vsg::PagedLOD::create();
    page->bound = vsg::dsphere(0.0,0.0,0.0,176.0);
    page->setValue("osgb.pixelSplit", 200.0);
    page->setValue("osgb.lodSphere", vsg::dvec4(0,0,0,100));
    page->children[1].node = vsg::Group::create();
    page->filename = "child.osgb";
    page->children[0].minimumScreenHeightRatio = 1000000.0;
    auto node = detail::installOSGBLOD(page);
    REQUIRE(std::string(node->className()) == "vsg::PagedLOD");
    vsg::VSG writer;
    std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
    REQUIRE(writer.write(node, stream, {}));
    stream.seekg(0);
    auto restored = writer.read(stream, {}).cast<vsg::Node>();
    REQUIRE(restored);
    vsg::dvec4 bound;
    REQUIRE(restored->getValue("osgb.lodSphere", bound));
    REQUIRE(bound.w == 100.0);
    auto installed = detail::installOSGBLOD(restored);
    REQUIRE(installed != restored);
    REQUIRE(installed.cast<vsg::PagedLOD>()->filename == "child.osgb");
    REQUIRE(detail::installOSGBLOD(installed) == installed);
    // 缓存恢复后实际 Record 仍按源范围选精模，不仅验证节点类型或元数据。
    auto traversal = vsg::RecordTraversal::create();
    traversal->frameStamp = vsg::FrameStamp::create();
    traversal->frameStamp->frameCount = 10;
    traversal->state->setProjectionAndViewMatrix(
        toVSG(osg::Matrixd::perspective(60,16.0/9.0,1,10000),true),
        toVSG(osg::Matrixd::translate(0,0,-1000),false));
    auto view = vsg::View::create();
    view->camera = vsg::Camera::create();
    view->camera->viewportState = vsg::ViewportState::create(0,0,1920,1080);
    traversal->viewDependentState = view->viewDependentState;
    restored->accept(*traversal);
    REQUIRE(restored.cast<vsg::PagedLOD>()->frameHighResLastUsed == 0);
    installed->accept(*traversal);
    REQUIRE(installed.cast<vsg::PagedLOD>()->frameHighResLastUsed == 10);
}

TEST_CASE("OSGB Record retries stale demand counters through one pager admission", "[osgb][lod][retry]")
{
    RetryFixture fixture;
    // 验证旧原子写入交错可达快照的需求恢复；本例不冒充完整线程交错的复现。
    // 请求入口完全来自 Record，不人为调用 Pager::request。
    fixture.page->requestStatus = vsg::PagedLOD::NoRequest;
    fixture.page->requestCount = 37;
    fixture.draw();
    REQUIRE(fixture.pager->metrics().queuedDetails == 1);
    REQUIRE(fixture.pager->numActiveRequests == 1);
    REQUIRE(fixture.page->requestStatus == vsg::PagedLOD::ReadRequest);
    CHECK(fixture.reads->selected == "coarse");
    for (unsigned repeat = 0; repeat < 20; ++repeat) fixture.draw();
    CHECK(fixture.pager->metrics().queuedDetails == 1);
    CHECK(fixture.pager->numActiveRequests == 1);
    fixture.pager->start(1);
    const bool ready = retryWait([&] { return fixture.pager->metrics().readyPages == 1; });
    REQUIRE(ready);
    fixture.update();
    REQUIRE(fixture.page->children[0].node);
    CHECK(fixture.pager->numActiveRequests == 0);
    CHECK(fixture.page->requestStatus == vsg::PagedLOD::NoRequest);
    fixture.draw();
    CHECK(fixture.reads->selected == "fine");
    CHECK(fixture.readCount() == 1);
    fixture.draw(true, 500.0);
    const double nearPriority = fixture.page->priority.load();
    fixture.draw(true, 1000.0);
    CHECK(fixture.page->priority.load() < nearPriority);
    CHECK(fixture.reads->selected == "fine");
    CHECK(fixture.readCount() == 1);
}

TEST_CASE("OSGB Record leaves an in-flight page and reloads it after reentry", "[osgb][lod][retry]")
{
    RetryFixture fixture;
    fixture.reads->blocked = true;
    fixture.pager->start(1);
    fixture.draw();
    const bool reading = retryWait([&] { return fixture.readCount() == 1; });
    REQUIRE(reading);
    REQUIRE(fixture.page->requestStatus == vsg::PagedLOD::Reading);
    REQUIRE(fixture.reads->selected == "coarse");
    // 实际改变 Record 视锥并推进帧，使后台读取必然在失去需求后结束。
    for (unsigned step = 0; step < 5; ++step)
    {
        ++fixture.frame;
        fixture.update();
        fixture.draw(false);
    }
    fixture.releaseRead();
    const bool cancelled = retryWait([&] {
        return fixture.pager->numActiveRequests == 0 && fixture.token->metrics().cancelled == 1;
    });
    REQUIRE(cancelled);
    CHECK(fixture.token->metrics().cancelled == 1);
    CHECK(fixture.pager->metrics().failed == 0);
    CHECK_FALSE(fixture.page->children[0].node);
    CHECK(fixture.page->requestStatus == vsg::PagedLOD::NoRequest);
    ++fixture.frame;
    fixture.update();
    fixture.draw();
    const bool ready = retryWait([&] { return fixture.pager->metrics().readyPages == 1; });
    REQUIRE(ready);
    fixture.update();
    fixture.draw();
    REQUIRE(fixture.page->children[0].node);
    CHECK(fixture.reads->selected == "fine");
    CHECK(fixture.readCount() == 2);
    CHECK(fixture.pager->numActiveRequests == 0);
    CHECK(fixture.token->metrics().merged == 1);
}
