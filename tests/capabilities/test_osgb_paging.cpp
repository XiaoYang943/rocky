#include "../globe/catch.hpp"
#include "../../src/render/import/osgb/OSGBLOD.h"
#include <geostudio/native/render/import/osgb/OSGBPaging.h>
#include <vsg/all.h>
#include <vsg/vk/State.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

using namespace geostudio::native::io;
namespace
{
// 内存 Reader 使用真实 Pager 线程；无磁盘、窗口、Device 或 GPU。
struct ReadState
{
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<std::string> files;
    bool blocked = false, released = false, emitPaged = false, textured = false;
    bool blockCompile = false, compileEntered = false;
    std::function<vsg::ref_ptr<vsg::Node>(const vsg::Path&, vsg::ref_ptr<const vsg::Options>)> makeNode;
};
// 编译阶段阻塞真实 CompileTraversal，CPU 用例不构造 Device，也不伪造 GPU 完成。
struct CompileGate : vsg::Inherit<vsg::Group, CompileGate>
{
    std::shared_ptr<ReadState> state;
    explicit CompileGate(std::shared_ptr<ReadState> input) : state(std::move(input)) {}
    void accept(vsg::Visitor& visitor) override
    {
        if (dynamic_cast<vsg::CompileTraversal*>(&visitor))
        {
            std::unique_lock lock(state->mutex);
            state->compileEntered = true;
            state->changed.notify_all();
            state->changed.wait(lock, [&] { return state->released; });
        }
        visitor.apply(static_cast<vsg::Group&>(*this));
    }
    void accept(vsg::ConstVisitor& visitor) const override { visitor.apply(static_cast<const vsg::Group&>(*this)); }
    void accept(vsg::RecordTraversal& visitor) const override { visitor.apply(static_cast<const vsg::Group&>(*this)); }
};
struct MemoryReader : vsg::Inherit<vsg::ReaderWriter, MemoryReader>
{
    std::shared_ptr<ReadState> state;
    explicit MemoryReader(std::shared_ptr<ReadState> input) : state(std::move(input)) {}
    vsg::ref_ptr<vsg::Object> read(const vsg::Path& filename, vsg::ref_ptr<const vsg::Options> options) const override
    {
        {
            std::unique_lock lock(state->mutex);
            state->files.push_back(filename.string());
            state->changed.notify_all();
            state->changed.wait(lock, [&] { return !state->blocked || state->released; });
        }
        if (filename == "missing.osgb") return {};
        if (state->makeNode) return state->makeNode(filename, options);
        auto draw = vsg::VertexDraw::create();
        draw->assignArrays(vsg::DataList{vsg::vec3Array::create(3)});
        draw->vertexCount = 3;
        draw->instanceCount = 1;
        if (state->blockCompile)
        {
            auto gate = CompileGate::create(state);
            gate->addChild(draw);
            return gate;
        }
        if (state->textured)
        {
            auto textured = vsg::StateGroup::create();
            auto pixels = vsg::ubvec4Array2D::create(64, 64, vsg::Data::Properties{VK_FORMAT_R8G8B8A8_SRGB});
            for (auto& pixel : *pixels) pixel = vsg::ubvec4(48, 96, 192, 255);
            auto image = vsg::DescriptorImage::create(vsg::Sampler::create(), pixels, 0);
            auto setLayout = vsg::DescriptorSetLayout::create(vsg::DescriptorSetLayoutBindings{
                {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}});
            auto layout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{setLayout}, vsg::PushConstantRanges{});
            auto descriptors = vsg::DescriptorSet::create(setLayout, vsg::Descriptors{image});
            textured->add(vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, descriptors));
            textured->addChild(draw);
            if (filename == "level2.osgb") return textured;
            auto level = vsg::PagedLOD::create();
            level->options = vsg::Options::create(*options);
            level->filename = filename == "level1.osgb" ? "level2.osgb" : "level1.osgb";
            level->bound = vsg::dsphere(0.0, 0.0, 0.0, 1.0);
            level->setValue("osgb.pixelSplit", 100.0);
            level->children[1].node = textured;
            return level;
        }
        if (state->emitPaged)
        {
            auto group = vsg::Group::create();
            group->addChild(draw);
            auto page = vsg::PagedLOD::create();
            page->bound = vsg::dsphere(0.0, 0.0, 0.0, 1.0);
            page->setValue("osgb.pixelSplit", 540.0);
            page->children[0].minimumScreenHeightRatio = 0.5;
            page->children[1].minimumScreenHeightRatio = 0.0;
            group->addChild(page);
            return group;
        }
        return draw;
    }
};
struct PagerFixture
{
    std::shared_ptr<ReadState> reads = std::make_shared<ReadState>();
    vsg::ref_ptr<vsg::Viewer> viewer = vsg::Viewer::create();
    vsg::ref_ptr<OSGBDatabasePager> pager = OSGBDatabasePager::create();
    vsg::ref_ptr<vsg::Options> options = vsg::Options::create();
    PagerFixture()
    {
        options->readerWriters = {MemoryReader::create(reads)};
        pager->compileManager = vsg::CompileManager::create(*viewer, vsg::ref_ptr<vsg::ResourceHints>{});
        pager->frameCount = 10;
    }
    ~PagerFixture()
    {
        { std::lock_guard lock(reads->mutex); reads->released = true; reads->changed.notify_all(); }
        pager->shutdown();
    }
    vsg::ref_ptr<vsg::PagedLOD> page(const char* name, bool root = false)
    {
        vsg::ref_ptr<vsg::PagedLOD> result = root ? vsg::ref_ptr<vsg::PagedLOD>(OSGBRoot::create()) : vsg::PagedLOD::create();
        result->filename = name;
        result->options = vsg::Options::create(*options);
        result->frameHighResLastUsed = 10;
        result->priority = root ? 1000000.0 : 2.0;
        result->requestCount = 1;
        return result;
    }
    vsg::CompileResult update(uint64_t frame = 10)
    {
        auto stamp = vsg::FrameStamp::create();
        stamp->frameCount = frame;
        vsg::CompileResult result;
        pager->updateSceneGraph(stamp, result);
        return result;
    }
};
bool waitFor(const std::function<bool()>& ready)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!ready() && std::chrono::steady_clock::now() < end) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    return ready();
}
bool disconnected(const vsg::Auxiliary& lifetime)
{
    std::lock_guard lock(lifetime.getMutex());
    return lifetime.getConnectedObject() == nullptr;
}

// 仅保存析构观察标记；不能由测试的观察器反向保活节点、几何或贴图。
struct PayloadObservations
{
    std::mutex mutex;
    std::map<std::string, std::vector<vsg::ref_ptr<vsg::Auxiliary>>> lifetimes;
    std::vector<std::string> selected;
    void observe(const std::string& name, vsg::Object& object)
    {
        std::lock_guard lock(mutex);
        lifetimes[name].emplace_back(object.getOrCreateAuxiliary());
    }
    bool released(const std::string& name)
    {
        std::lock_guard lock(mutex);
        const auto found = lifetimes.find(name);
        return found != lifetimes.end() && !found->second.empty() &&
            std::all_of(found->second.begin(), found->second.end(), [](const auto& item) { return disconnected(*item); });
    }
    bool allReleased()
    {
        std::lock_guard lock(mutex);
        return !lifetimes.empty() && std::all_of(lifetimes.begin(), lifetimes.end(), [](const auto& item) {
            return std::all_of(item.second.begin(), item.second.end(), [](const auto& lifetime) { return disconnected(*lifetime); });
        });
    }
    bool drew(const std::string& name)
    {
        std::lock_guard lock(mutex);
        return std::find(selected.begin(), selected.end(), name) != selected.end();
    }
};

// 保留真实 DescriptorImage/VertexDraw 供 CPU 编译与载荷统计，末端 Record 只记选择，避免调用 Vulkan。
struct RecordLeaf : vsg::Inherit<vsg::StateGroup, RecordLeaf>
{
    std::shared_ptr<PayloadObservations> observations;
    std::string name;
    RecordLeaf(std::shared_ptr<PayloadObservations> input, std::string label) : observations(std::move(input)), name(std::move(label)) {}
    void accept(vsg::RecordTraversal&) const override
    {
        std::lock_guard lock(observations->mutex);
        observations->selected.push_back(name);
    }
};
constexpr uint64_t recordedPageBytes = 4 * 4 * 4 + 3 * sizeof(vsg::vec3);
vsg::ref_ptr<vsg::Node> recordedLeaf(const std::shared_ptr<PayloadObservations>& observations, const std::string& name, double x = 0.0)
{
    auto leaf = RecordLeaf::create(observations, name);
    auto positions = vsg::vec3Array::create(3);
    (*positions)[0] = vsg::vec3(float(x - 0.5), -0.5f, 0.0f);
    (*positions)[1] = vsg::vec3(float(x + 0.5), -0.5f, 0.0f);
    (*positions)[2] = vsg::vec3(float(x), 0.5f, 0.0f);
    auto draw = vsg::VertexDraw::create();
    draw->assignArrays(vsg::DataList{positions}); draw->vertexCount = 3; draw->instanceCount = 1;
    auto pixels = vsg::ubvec4Array2D::create(4, 4, vsg::Data::Properties{VK_FORMAT_R8G8B8A8_SRGB});
    for (auto& pixel : *pixels) pixel = vsg::ubvec4(48, 96, 192, 255);
    auto image = vsg::DescriptorImage::create(vsg::Sampler::create(), pixels, 0);
    auto setLayout = vsg::DescriptorSetLayout::create(vsg::DescriptorSetLayoutBindings{
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}});
    auto layout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{setLayout}, vsg::PushConstantRanges{});
    leaf->add(vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0,
        vsg::DescriptorSet::create(setLayout, vsg::Descriptors{image})));
    leaf->addChild(draw);
    observations->observe(name, *leaf);
    observations->observe(name, *positions);
    observations->observe(name, *pixels);
    return leaf;
}
vsg::ref_ptr<vsg::Node> recordedSource(const std::shared_ptr<PayloadObservations>& observations, const std::string& name,
    const std::string& filename, vsg::ref_ptr<const vsg::Options> options, double x = 0.0, bool group = false)
{
    auto page = vsg::PagedLOD::create();
    page->options = vsg::Options::create(*options);
    page->filename = filename;
    page->bound = vsg::dsphere(x, 0.0, 0.0, 1.0);
    page->setValue("osgb.distanceSplit", 20.0);
    page->setValue("osgb.distanceSphere", vsg::dvec4(x, 0.0, 0.0, 1.0));
    page->children[1].node = recordedLeaf(observations, name, x);
    auto installed = detail::installOSGBLOD(page);
    observations->observe(name + ".page", *installed);
    if (!group) return installed;
    auto parent = vsg::Group::create();
    parent->addChild(installed);
    observations->observe(name + ".group", *parent);
    return parent;
}

// 真实 SourceLOD/OSGBRoot Record 产生需求，再由真实 Pager 线程读取与 Update 合并；不手改最近使用帧。
struct RecordPagerFixture : PagerFixture
{
    std::shared_ptr<PayloadObservations> observations = std::make_shared<PayloadObservations>();
    vsg::ref_ptr<vsg::RecordTraversal> traversal = vsg::RecordTraversal::create();
    uint64_t frame = 10;
    RecordPagerFixture()
    {
        traversal->setDatabasePager(pager);
        OSGBPagingLimits limits;
        limits.maxMergesPerFrame = 1;
        limits.mergeTimeMs = 0.0;
        limits.recentResidentSeconds = 0.0;
        limits.maxInactivePages = 0;
        pager->configure(limits);
    }
    vsg::ref_ptr<OSGBRoot> root(const char* name, vsg::ref_ptr<OSGBPagingToken> token, double x = 0.0)
    {
        auto result = OSGBRoot::create();
        result->filename = name;
        result->options = vsg::Options::create(*options);
        result->options->setObject(osgbPagingTokenKey, token);
        result->bound = vsg::dsphere(x, 0.0, 0.0, 1.0);
        return result;
    }
    void advance(vsg::Node* scene, double x = 0.0, double z = 10.0)
    {
        auto stamp = vsg::FrameStamp::create(); stamp->frameCount = ++frame;
        traversal->setFrameStamp(stamp);
        traversal->state->setProjectionAndViewMatrix(vsg::orthographic(-6.0, 6.0, -4.0, 4.0, 1.0, 1000.0),
            vsg::translate(-x, 0.0, -z));
        { std::lock_guard lock(observations->mutex); observations->selected.clear(); }
        if (scene) scene->accept(*traversal);
        update(frame);
    }
    void budget(uint64_t pages)
    {
        OSGBMemoryLimits limits;
        limits.maxPageBytes = limits.readReservationBytes = recordedPageBytes;
        limits.readWorkingBytes = 2 * recordedPageBytes;
        limits.residentBytes = pages * recordedPageBytes;
        pager->configureMemory(limits);
        OSGBResidencyLimits residency;
        residency.transitionBytes = residency.detailReserveBytes = 0;
        pager->configureResidency(residency);
    }
};
}

TEST_CASE("OSGB root keeps paged visitor dispatch with finite priority", "[osgb-paging]")
{
    CHECK(std::isfinite(OSGBRoot::projectedPriority(100.0, 0.0)));
    CHECK(std::isfinite(OSGBRoot::projectedPriority(std::numeric_limits<double>::infinity(), 0.0)));
    CHECK(OSGBRoot::projectedPriority(100.0, 100.0) > OSGBRoot::projectedPriority(1.0, 100.0));
    auto root = OSGBRoot::create();
    root->bound = vsg::dsphere(10.0, 20.0, 30.0, 5.0);
    vsg::ComputeBounds bounds;
    root->accept(bounds);
    REQUIRE(bounds.bounds.valid());
    CHECK(bounds.bounds.min.x == Approx(5.0));
    CHECK(root->clone().cast<OSGBRoot>());
    auto viewer = vsg::Viewer::create();
    auto compiler = vsg::CompileManager::create(*viewer, vsg::ref_ptr<vsg::ResourceHints>{});
    const auto result = compiler->compile(root);
    REQUIRE(result);
    CHECK(result.containsPagedLOD);
}

TEST_CASE("OSGB pager makes fair progress and limits each update merge", "[osgb-paging]")
{
    PagerFixture fixture;
    OSGBPagingLimits limits;
    limits.maxMergesPerFrame = 1;
    fixture.pager->configure(limits);
    auto root1 = fixture.page("root1.osgb", true), root2 = fixture.page("root2.osgb", true);
    auto detail1 = fixture.page("detail1.osgb"), detail2 = fixture.page("detail2.osgb");
    for (auto page : {root1, root2, detail1, detail2}) fixture.pager->request(page);
    fixture.pager->start(1);
    const bool ready = waitFor([&] { return fixture.pager->metrics().readyPages == 4; });
    REQUIRE(ready);
    {
        std::lock_guard lock(fixture.reads->mutex);
        REQUIRE(fixture.reads->files.size() == 4);
        CHECK(fixture.reads->files[0] == "root1.osgb");
        CHECK(fixture.reads->files[1] == "detail1.osgb");
        CHECK(fixture.reads->files[2] == "root2.osgb");
        CHECK(fixture.reads->files[3] == "detail2.osgb");
    }
    REQUIRE(fixture.update());
    CHECK(fixture.pager->metrics().merged == 1);
    CHECK(fixture.pager->metrics().readyPages == 3);
    CHECK(root1->children[0].node);
    for (unsigned i = 0; i < 3; ++i) REQUIRE(fixture.update());
    CHECK(fixture.pager->numActiveRequests == 0);
    CHECK(root2->children[0].node);
    CHECK(detail1->children[0].node);
    CHECK(detail2->children[0].node);
    CHECK(fixture.pager->compileManager->successfulCompileCount == 4);
}

TEST_CASE("OSGB pager rejects closed or stale completed pages and preserves coarse content", "[osgb-paging]")
{
    PagerFixture fixture;
    auto token = OSGBPagingToken::create();
    fixture.options->setObject(osgbPagingTokenKey, token);
    auto page = fixture.page("late.osgb");
    auto coarse = vsg::Group::create();
    page->children[1].node = coarse;
    fixture.pager->request(page);
    fixture.pager->start(1);
    const bool ready = waitFor([&] { return fixture.pager->metrics().readyPages == 1; });
    REQUIRE(ready);
    token->close();
    fixture.update();
    CHECK_FALSE(page->children[0].node);
    CHECK(page->children[1].node == coarse);
    CHECK(fixture.pager->numActiveRequests == 0);
    CHECK(fixture.pager->metrics().cancelled == 1);
    CHECK(fixture.pager->metrics().readyCpuBytes == 0);
    CHECK(token->metrics().ready == 0);
    CHECK(token->metrics().cancelled == 1);
    auto generic = fixture.page("generic.osgb");
    generic->options = vsg::Options::create();
    generic->options->readerWriters = {MemoryReader::create(fixture.reads)};
    fixture.pager->request(generic);
    const bool genericReady = waitFor([&] { return fixture.pager->metrics().readyPages == 1; });
    REQUIRE(genericReady);
    fixture.update(20);
    CHECK_FALSE(generic->children[0].node);
    CHECK(fixture.pager->metrics().cancelled == 2);
}

TEST_CASE("OSGB cancellation during real background read does not compile a late result", "[osgb-paging]")
{
    PagerFixture fixture;
    auto token = OSGBPagingToken::create();
    fixture.options->setObject(osgbPagingTokenKey, token);
    fixture.reads->blocked = true;
    auto page = fixture.page("reading.osgb");
    fixture.pager->request(page);
    fixture.pager->start(1);
    const bool reading = waitFor([&] { std::lock_guard lock(fixture.reads->mutex); return !fixture.reads->files.empty(); });
    REQUIRE(reading);
    CHECK(token->metrics().reading == 1);
    CHECK(token->metrics().queued == 0);
    token->close();
    { std::lock_guard lock(fixture.reads->mutex); fixture.reads->released = true; fixture.reads->changed.notify_all(); }
    const bool cancelled = waitFor([&] { return fixture.pager->numActiveRequests == 0; });
    REQUIRE(cancelled);
    fixture.update();
    CHECK_FALSE(page->children[0].node);
    CHECK(fixture.pager->compileManager->successfulCompileCount == 0);
    CHECK(fixture.pager->metrics().activeDetails == 0);
    CHECK(token->metrics().reading == 0);
    CHECK(token->metrics().cancelled == 1);
}

TEST_CASE("OSGB byte target retains visible roots and retires inactive pages", "[osgb-paging]")
{
    PagerFixture fixture;
    fixture.options->setObject(osgbPagingTokenKey, OSGBPagingToken::create());
    OSGBPagingLimits limits;
    limits.readyCpuBytes = 1;
    limits.residentBytesTarget = 1;
    fixture.pager->configure(limits);
    auto root = fixture.page("visible.osgb", true);
    fixture.pager->request(root);
    fixture.pager->start(1);
    const bool ready = waitFor([&] { return fixture.pager->metrics().readyPages == 1; });
    REQUIRE(ready);
    REQUIRE(fixture.pager->metrics().readyCpuBytes > 1);
    REQUIRE(fixture.update());
    CHECK(root->children[0].node);
    CHECK(fixture.pager->metrics().oversizePages == 1);
    CHECK(fixture.pager->metrics().residentEstimatedBytes > 1);
    fixture.update(20);
    CHECK_FALSE(root->children[0].node);
    CHECK(fixture.pager->metrics().residentEstimatedBytes == 0);
    CHECK(fixture.pager->pagedLODContainer->activeList.count == 0);
    CHECK(fixture.pager->pagedLODContainer->inactiveList.count == 0);
}

TEST_CASE("OSGB installation requires viewer tasks and pager start is idempotent", "[osgb-paging]")
{
    // RecordAndSubmitTask 构造会创建真实 Fence/Semaphore，CPU 测试不伪造空 Device。
    auto viewer = vsg::Viewer::create();
    CHECK_FALSE(prepareOSGBPaging(*viewer));
    viewer->compileManager = vsg::CompileManager::create(*viewer, vsg::ref_ptr<vsg::ResourceHints>{});
    CHECK_FALSE(installOSGBPaging(*viewer));
    auto pager = OSGBDatabasePager::create();
    pager->compileManager = viewer->compileManager;
    pager->start(1);
    pager->start(4);
    CHECK(pager->threads.size() == 2);
    pager->shutdown();
    pager->start(1);
    CHECK(pager->threads.empty());
}

TEST_CASE("OSGB memory reservations bound readers before decoding", "[osgb-paging][memory]")
{
    PagerFixture fixture;
    fixture.options->setObject(osgbPagingTokenKey, OSGBPagingToken::create());
    OSGBMemoryLimits limits;
    limits.maxPageBytes = limits.readReservationBytes = limits.readWorkingBytes = 36;
    limits.residentBytes = 72;
    fixture.pager->configureMemory(limits);
    fixture.reads->blocked = true;
    fixture.pager->request(fixture.page("first.osgb"));
    fixture.pager->request(fixture.page("second.osgb"));
    fixture.pager->start(4);
    const bool memoryCheck1 = waitFor([&] { std::lock_guard lock(fixture.reads->mutex); return fixture.reads->files.size() == 1; });
    REQUIRE(memoryCheck1);
    const bool memoryCheck2 = waitFor([&] { return fixture.pager->memoryMetrics().budgetDeferrals > 0; });
    REQUIRE(memoryCheck2);
    { std::lock_guard lock(fixture.reads->mutex); CHECK(fixture.reads->files.size() == 1); fixture.reads->released = true; fixture.reads->changed.notify_all(); }
    const bool memoryCheck3 = waitFor([&] { return fixture.pager->metrics().readyPages == 2; });
    REQUIRE(memoryCheck3);
    CHECK(fixture.pager->memoryMetrics().readReservedBytes == 0);
}

TEST_CASE("OSGB resident admission blocks even visible refinements and resumes after retirement", "[osgb-paging][memory]")
{
    PagerFixture fixture;
    fixture.options->setObject(osgbPagingTokenKey, OSGBPagingToken::create());
    OSGBMemoryLimits limits;
    limits.maxPageBytes = limits.readReservationBytes = limits.readWorkingBytes = limits.residentBytes = 36;
    fixture.pager->configureMemory(limits);
    OSGBPagingLimits paging;
    paging.recentResidentSeconds = 0.001;
    fixture.pager->configure(paging);
    auto first = fixture.page("first.osgb"), next = fixture.page("next.osgb");
    fixture.pager->request(first);
    fixture.pager->start(2);
    const bool memoryCheck4 = waitFor([&] { return fixture.pager->metrics().readyPages == 1; });
    REQUIRE(memoryCheck4);
    fixture.update();
    REQUIRE(first->children[0].node);
    vsg::ref_ptr<vsg::Auxiliary> old(first->children[0].node->getOrCreateAuxiliary());
    fixture.pager->releaseUploadedData(0);
    CHECK(fixture.pager->memoryMetrics().cpuBytesReleased == 0); // 无 GPU 上传时必须保留数据。
    CHECK(fixture.pager->memoryMetrics().residentCpuBytes == 36);
    fixture.pager->request(next);
    const bool memoryCheck5 = waitFor([&] { return fixture.pager->memoryMetrics().budgetDeferrals > 0; });
    REQUIRE(memoryCheck5);
    CHECK(fixture.pager->metrics().readyPages == 0);
    // 剩余预算允许未知页探测一次；发现实测尺寸放不下后只保留轻量等待请求。
    const bool measured = waitFor([&] {
        return next->loadAttempts.load() == 1 && next->requestStatus == vsg::PagedLOD::ReadRequest;
    });
    REQUIRE(measured);
    { std::lock_guard lock(fixture.reads->mutex); CHECK(fixture.reads->files.size() == 2); }
    CHECK(fixture.pager->residencyMetrics().budgetBlockedPages == 1);
    CHECK(fixture.pager->residencyMetrics().readReservedBytes == 0);
    uint64_t frame = 20;
    const bool memoryCheck6 = waitFor([&] {
        next->frameHighResLastUsed = frame;
        fixture.update(frame++);
        return bool(next->children[0].node) && disconnected(*old);
    });
    REQUIRE(memoryCheck6);
    CHECK_FALSE(first->children[0].node);
    CHECK(fixture.pager->metrics().residentEstimatedBytes == 36);
}

TEST_CASE("OSGB oversized budget wait preserves fallback and resumes without reloading layer", "[osgb-paging][memory]")
{
    PagerFixture fixture;
    fixture.options->setObject(osgbPagingTokenKey, OSGBPagingToken::create());
    OSGBMemoryLimits limits;
    limits.maxPageBytes = limits.readReservationBytes = limits.readWorkingBytes = limits.residentBytes = 35;
    fixture.pager->configureMemory(limits);
    auto page = fixture.page("large.osgb");
    auto coarse = vsg::Group::create(); page->children[1].node = coarse;
    fixture.pager->request(page); fixture.pager->start(1);
    const bool memoryCheck7 = waitFor([&] { return fixture.pager->residencyMetrics().oversizedWaitingPages == 1; });
    REQUIRE(memoryCheck7);
    CHECK(fixture.pager->memoryMetrics().oversizedRejected == 1);
    CHECK(page->frameNextLoadAttempt != std::numeric_limits<uint64_t>::max());
    CHECK(fixture.pager->metrics().failed == 0);
    CHECK(fixture.pager->memoryMetrics().readReservedBytes == 0);
    CHECK(fixture.pager->compileManager->successfulCompileCount == 0);
    CHECK(page->children[1].node == coarse);
    CHECK_FALSE(page->children[0].node);
    limits.maxPageBytes = limits.readReservationBytes = limits.readWorkingBytes = limits.residentBytes = 36;
    fixture.pager->configureMemory(limits);
    {
        const bool completed = waitFor([&] { return fixture.pager->metrics().readyPages == 1; });
        REQUIRE(completed);
    }
    fixture.update();
    CHECK(page->children[0].node);
    CHECK(fixture.pager->residencyMetrics().budgetBlockedPages == 0);
}

TEST_CASE("OSGB pressure never changes LOD thresholds or carries bias to another layer", "[osgb-paging][memory]")
{
    PagerFixture fixture;
    fixture.options->setObject(osgbPagingTokenKey, OSGBPagingToken::create());
    fixture.reads->emitPaged = true;
    OSGBMemoryLimits limits;
    limits.maxPageBytes = limits.readReservationBytes = limits.readWorkingBytes = limits.residentBytes = 36;
    fixture.pager->configureMemory(limits);
    auto page = fixture.page("root.osgb", true);
    fixture.pager->request(page); fixture.pager->start(1);
    const bool memoryCheck8 = waitFor([&] { return fixture.pager->metrics().readyPages == 1; });
    REQUIRE(memoryCheck8);
    fixture.update();
    auto group = page->children[0].node.cast<vsg::Group>();
    REQUIRE(group);
    auto detail = group->children.back().cast<vsg::PagedLOD>();
    REQUIRE(detail);
    CHECK(detail->children[0].minimumScreenHeightRatio == Approx(0.5));
    page->frameHighResLastUsed = 40;
    fixture.update(40);
    CHECK(fixture.pager->memoryMetrics().lodScale == 1.0);
    CHECK(detail->children[0].minimumScreenHeightRatio == Approx(0.5));
    adjustOSGBLOD(*group, 2160, 2.0);
    CHECK(detail->children[0].minimumScreenHeightRatio == Approx(0.25));
}

TEST_CASE("OSGB repeated close releases scene payload and retirement accounting", "[osgb-paging][memory]")
{
    PagerFixture fixture;
    fixture.pager->start(2);
    uint64_t frame = 10;
    for (unsigned cycle = 0; cycle < 20; ++cycle)
    {
        auto token = OSGBPagingToken::create();
        fixture.options->setObject(osgbPagingTokenKey, token);
        auto page = fixture.page("reopened.osgb", true);
        page->frameHighResLastUsed = frame;
        fixture.pager->request(page);
        const bool ready = waitFor([&] { return fixture.pager->metrics().readyPages == 1; });
        REQUIRE(ready);
        fixture.update(frame);
        REQUIRE(page->children[0].node);
        vsg::ref_ptr<vsg::Auxiliary> node(page->children[0].node->getOrCreateAuxiliary());
        auto draw = page->children[0].node.cast<vsg::VertexDraw>();
        REQUIRE(draw);
        vsg::ref_ptr<vsg::Auxiliary> data(draw->arrays.front()->data->getOrCreateAuxiliary());
        draw = {};
        token->close();
        const bool retired = waitFor([&] {
            fixture.update(++frame);
            return disconnected(*node) && disconnected(*data) && fixture.pager->memoryMetrics().retiringBytes == 0;
        });
        REQUIRE(retired);
        CHECK(fixture.pager->metrics().residentEstimatedBytes == 0);
        CHECK(fixture.pager->memoryMetrics().readReservedBytes == 0);
        CHECK(fixture.pager->pagedLODContainer->activeList.count == 0);
        CHECK(fixture.pager->pagedLODContainer->inactiveList.count == 0);
    }
}

TEST_CASE("OSGB unknown small pages progress when conservative estimates exceed remaining space", "[osgb-paging][memory]")
{
    PagerFixture fixture;
    fixture.options->setObject(osgbPagingTokenKey, OSGBPagingToken::create());
    OSGBMemoryLimits memory;
    memory.maxPageBytes = memory.readReservationBytes = memory.readWorkingBytes = memory.residentBytes = 72;
    fixture.pager->configureMemory(memory);
    fixture.pager->start(1);
    auto first = fixture.page("small-first.osgb"), next = fixture.page("small-next.osgb");
    fixture.pager->request(first);
    {
        const bool completed = waitFor([&] { return fixture.pager->metrics().readyPages == 1; });
        REQUIRE(completed);
    }
    fixture.update();
    REQUIRE(first->children[0].node);
    fixture.pager->request(next);
    {
        const bool completed = waitFor([&] { return fixture.pager->metrics().readyPages == 1; });
        REQUIRE(completed);
    }
    fixture.update();
    CHECK(next->children[0].node);
    CHECK(fixture.pager->metrics().residentEstimatedBytes == 72);
    CHECK(fixture.pager->residencyMetrics().budgetBlockedPages == 0);
}

TEST_CASE("OSGB known large pages reserve their measured read payload", "[osgb-paging][memory]")
{
    PagerFixture fixture;
    fixture.options->setObject(osgbPagingTokenKey, OSGBPagingToken::create());
    OSGBMemoryLimits memory;
    memory.maxPageBytes = memory.readReservationBytes = 18;
    memory.readWorkingBytes = 35;
    memory.residentBytes = 144;
    fixture.pager->configureMemory(memory);
    fixture.pager->request(fixture.page("measured-a.osgb"));
    fixture.pager->request(fixture.page("measured-b.osgb"));
    fixture.pager->start(2);
    {
        const bool completed = waitFor([&] { return fixture.pager->residencyMetrics().oversizedWaitingPages == 2; });
        REQUIRE(completed);
    }
    { std::lock_guard lock(fixture.reads->mutex); fixture.reads->blocked = true; CHECK(fixture.reads->files.size() == 2); }
    const auto before = fixture.pager->memoryMetrics().budgetDeferrals;
    memory.readWorkingBytes = 54;
    fixture.pager->configureMemory(memory);
    {
        const bool completed = waitFor([&] {
            return fixture.pager->metrics().activeDetails == 2 ||
                fixture.pager->memoryMetrics().budgetDeferrals > before + 32;
        });
        REQUIRE(completed);
    }
    CHECK(fixture.pager->metrics().activeDetails == 1);
    CHECK(fixture.pager->residencyMetrics().readReservedBytes == 36);
    { std::lock_guard lock(fixture.reads->mutex); CHECK(fixture.reads->files.size() == 3); fixture.reads->released = true; fixture.reads->changed.notify_all(); }
    {
        const bool completed = waitFor([&] { return fixture.pager->metrics().readyPages == 2; });
        REQUIRE(completed);
    }
    CHECK(fixture.pager->metrics().failed == 0);
}

TEST_CASE("OSGB soft ready byte reservations serialize concurrent compilation", "[osgb-paging][memory]")
{
    PagerFixture fixture;
    fixture.options->setObject(osgbPagingTokenKey, OSGBPagingToken::create());
    fixture.reads->blockCompile = true;
    OSGBPagingLimits paging;
    paging.readyCpuBytes = 36;
    paging.maxMergesPerFrame = 1;
    fixture.pager->configure(paging);
    for (const auto* name : {"slot-a.osgb", "slot-b.osgb", "slot-c.osgb"}) fixture.pager->request(fixture.page(name));
    fixture.pager->start(3);
    {
        const bool completed = waitFor([&] { return fixture.pager->residencyMetrics().decodedCpuBytes == 72; });
        REQUIRE(completed);
    }
    CHECK(fixture.pager->residencyMetrics().compilingCpuBytes == 36);
    CHECK(fixture.pager->metrics().readyPages == 0);
    { std::lock_guard lock(fixture.reads->mutex); fixture.reads->released = true; fixture.reads->changed.notify_all(); }
    for (unsigned page = 0; page < 3; ++page)
    {
        {
            const bool completed = waitFor([&] { return fixture.pager->metrics().readyPages == 1; });
            REQUIRE(completed);
        }
        CHECK(fixture.pager->metrics().readyCpuBytes == 36);
        fixture.update();
    }
    CHECK(fixture.pager->numActiveRequests == 0);
    CHECK(fixture.pager->residencyMetrics().decodedCpuBytes == 0);
    CHECK(fixture.pager->residencyMetrics().compilingCpuBytes == 0);
}

TEST_CASE("OSGB cancellation during compile retires pending work without a late merge", "[osgb-paging][memory]")
{
    PagerFixture fixture;
    auto token = OSGBPagingToken::create();
    fixture.options->setObject(osgbPagingTokenKey, token);
    fixture.reads->blockCompile = true;
    auto page = fixture.page("compile-cancel.osgb");
    fixture.pager->request(page); fixture.pager->start(1);
    {
        const bool completed = waitFor([&] { std::lock_guard lock(fixture.reads->mutex); return fixture.reads->compileEntered; });
        REQUIRE(completed);
    }
    CHECK(token->metrics().compiling == 1);
    CHECK(fixture.pager->residencyMetrics().compilingCpuBytes == 36);
    token->close();
    fixture.update();
    { std::lock_guard lock(fixture.reads->mutex); fixture.reads->released = true; fixture.reads->changed.notify_all(); }
    {
        const bool completed = waitFor([&] { return fixture.pager->numActiveRequests == 0; });
        REQUIRE(completed);
    }
    uint64_t frame = 10;
    {
        const bool completed = waitFor([&] { fixture.update(++frame); return fixture.pager->residencyMetrics().retiringPages == 0; });
        REQUIRE(completed);
    }
    const auto memory = fixture.pager->residencyMetrics();
    CHECK(memory.residentPages == 0);
    CHECK(memory.readyCpuBytes == 0);
    CHECK(memory.compilingCpuBytes == 0);
    CHECK(memory.decodedCpuBytes == 0);
    CHECK(memory.readReservedBytes == 0);
    CHECK(memory.readClaimBytes == 0);
    CHECK_FALSE(page->pending);
    CHECK_FALSE(page->children[0].node);
    CHECK(token->metrics().cancelled == 1);
    CHECK(fixture.pager->metrics().failed == 0);
}

TEST_CASE("OSGB textured three-level layers repeatedly unload independently", "[osgb-paging][memory]")
{
    PagerFixture fixture;
    fixture.reads->textured = true;
    fixture.pager->start(2);
    constexpr uint64_t pageBytes = 64 * 64 * 4 + 3 * sizeof(vsg::vec3);
    uint64_t frame = 10;
    for (unsigned cycle = 0; cycle < 6; ++cycle)
    {
        std::vector<vsg::ref_ptr<vsg::PagedLOD>> layers[2];
        vsg::ref_ptr<OSGBPagingToken> tokens[2] = {OSGBPagingToken::create(), OSGBPagingToken::create()};
        std::vector<vsg::ref_ptr<vsg::Auxiliary>> payloads[2];
        auto touch = [&] { for (auto& layer : layers) for (auto& page : layer) page->frameHighResLastUsed = frame; };
        for (unsigned layer = 0; layer < 2; ++layer)
        {
            fixture.options->setObject(osgbPagingTokenKey, tokens[layer]);
            auto page = fixture.page("level0.osgb", true);
            for (unsigned level = 0; level < 3; ++level)
            {
                layers[layer].push_back(page);
                page->frameHighResLastUsed = frame;
                fixture.pager->request(page);
                {
                    const bool completed = waitFor([&] { return fixture.pager->metrics().readyPages == 1; });
                    REQUIRE(completed);
                }
                touch(); fixture.update(frame);
                REQUIRE(page->children[0].node);
                struct Payloads : vsg::CollectResourceRequirements
                {
                    std::vector<vsg::ref_ptr<vsg::Auxiliary>>& payloads;
                    explicit Payloads(std::vector<vsg::ref_ptr<vsg::Auxiliary>>& value) : payloads(value) {}
                    void apply(vsg::ref_ptr<vsg::ImageInfo> image) override
                    { payloads.emplace_back(image->imageView->image->data->getOrCreateAuxiliary()); }
                    void apply(vsg::ref_ptr<vsg::BufferInfo> buffer, BufferProperties) override
                    { if (buffer->data) payloads.emplace_back(buffer->data->getOrCreateAuxiliary()); }
                } collect(payloads[layer]);
                page->children[0].node->accept(collect);
                if (level < 2)
                {
                    page = page->children[0].node.cast<vsg::PagedLOD>();
                    REQUIRE(page);
                }
            }
        }
        REQUIRE(fixture.pager->residencyMetrics().residentGpuEstimatedBytes == 6 * pageBytes);
        fixture.pager->releaseUploadedData(0);
        CHECK(fixture.pager->residencyMetrics().residentCpuBytes == 6 * pageBytes);
        CHECK(fixture.pager->memoryMetrics().cpuBytesReleased == 0);
        for (unsigned layer = 0; layer < 2; ++layer)
        {
            tokens[layer]->close();
            layers[layer].clear();
            {
                const bool completed = waitFor([&] {
                    ++frame; touch(); fixture.update(frame);
                    return std::all_of(payloads[layer].begin(), payloads[layer].end(), [](const auto& p) { return disconnected(*p); }) &&
                        fixture.pager->residencyMetrics().retiringPages == 0;
                });
                REQUIRE(completed);
            }
            const auto metrics = fixture.pager->residencyMetrics();
            CHECK(metrics.residentPages == (layer == 0 ? 3 : 0));
            CHECK(metrics.residentCpuBytes == (layer == 0 ? 3 * pageBytes : 0));
            CHECK(metrics.residentGpuEstimatedBytes == (layer == 0 ? 3 * pageBytes : 0));
            CHECK(metrics.budgetBlockedPages == 0);
            CHECK(metrics.decodedCpuBytes == 0);
            CHECK(metrics.compilingCpuBytes == 0);
            CHECK(metrics.readyCpuBytes == 0);
            CHECK(metrics.readReservedBytes == 0);
            CHECK(tokens[layer]->metrics().residentPages == 0);
            if (layer == 0)
            {
                CHECK(tokens[1]->metrics().residentPages == 3);
                const bool otherLayerAlive = std::none_of(payloads[1].begin(), payloads[1].end(), [](const auto& p) { return disconnected(*p); });
                CHECK(otherLayerAlive);
            }
        }
    }
}

TEST_CASE("OSGB eviction stops after reaching the byte target", "[osgb-paging][memory]")
{
    PagerFixture fixture;
    fixture.options->setObject(osgbPagingTokenKey, OSGBPagingToken::create());
    OSGBPagingLimits paging;
    paging.residentBytesTarget = 72;
    paging.recentResidentSeconds = 0.0;
    paging.maxInactivePages = 0;
    fixture.pager->configure(paging);
    OSGBMemoryLimits limits;
    limits.maxPageBytes = limits.readReservationBytes = 36;
    limits.readWorkingBytes = limits.residentBytes = 144;
    fixture.pager->configureMemory(limits);
    for (const auto* name : {"one.osgb", "two.osgb", "three.osgb"}) fixture.pager->request(fixture.page(name));
    fixture.pager->start(2);
    {
        const bool completed = waitFor([&] { return fixture.pager->metrics().readyPages == 3; });
        REQUIRE(completed);
    }
    while (fixture.pager->metrics().readyPages) fixture.update();
    REQUIRE(fixture.pager->metrics().residentEstimatedBytes == 108);
    fixture.update(20);
    CHECK(fixture.pager->metrics().residentEstimatedBytes == 72);
    CHECK(fixture.pager->residencyMetrics().residentPages == 2);
}

TEST_CASE("OSGB pressure does not evict inactive pages from other formats", "[osgb-paging][memory]")
{
    PagerFixture fixture;
    OSGBPagingLimits paging;
    paging.residentBytesTarget = 1;
    fixture.pager->configure(paging);
    auto generic = fixture.page("other-format.osgb");
    auto osgb = fixture.page("budgeted.osgb");
    osgb->options->setObject(osgbPagingTokenKey, OSGBPagingToken::create());
    fixture.pager->request(generic); fixture.pager->request(osgb);
    fixture.pager->start(2);
    const bool ready = waitFor([&] { return fixture.pager->metrics().readyPages == 2; });
    REQUIRE(ready);
    while (fixture.pager->metrics().readyPages) fixture.update();
    REQUIRE(generic->children[0].node);
    REQUIRE(osgb->children[0].node);
    fixture.update(20);
    CHECK_FALSE(osgb->children[0].node);
    CHECK(generic->children[0].node);
    CHECK(generic->pending);
    CHECK(generic->index != 0);
    CHECK(fixture.pager->pagedLODContainer->inactiveList.count == 1);
}

TEST_CASE("OSGB root coverage progresses before optional refinement consumes the remaining budget", "[osgb-paging][memory]")
{
    PagerFixture fixture;
    fixture.options->setObject(osgbPagingTokenKey, OSGBPagingToken::create());
    OSGBMemoryLimits memory;
    memory.residentBytes = 96;
    memory.maxPageBytes = memory.readReservationBytes = 36;
    memory.readWorkingBytes = 72;
    fixture.pager->configureMemory(memory);
    auto a = fixture.page("root-a.osgb", true), b = fixture.page("root-b.osgb", true);
    fixture.pager->request(a); fixture.pager->request(b); fixture.pager->start(2);
    {
        const bool completed = waitFor([&] { return fixture.pager->metrics().readyPages == 2; });
        REQUIRE(completed);
    }
    while (fixture.pager->metrics().readyPages) fixture.update();
    auto nextRoot = fixture.page("root-wait.osgb", true), detail = fixture.page("fine.osgb");
    fixture.pager->request(nextRoot); fixture.pager->request(detail);
    {
        const bool completed = waitFor([&] { return fixture.pager->metrics().readyPages == 1; });
        REQUIRE(completed);
    }
    fixture.update();
    CHECK(nextRoot->children[0].node);
    CHECK_FALSE(detail->children[0].node);
    CHECK(fixture.pager->metrics().residentEstimatedBytes == 108);
    CHECK(fixture.pager->residencyMetrics().transitionLimit == 24);
    CHECK(fixture.pager->residencyMetrics().budgetBlockedPages == 1);
    CHECK(fixture.pager->memoryMetrics().lodScale == 1.0);
}

TEST_CASE("OSGB Record closes textured pages retained before a fine merge", "[osgb-paging][memory][record]")
{
    for (const bool group : {false, true})
    for (const std::string waiting : {"ready", "budget", "read", "compile"})
    {
        CAPTURE(group);
        CAPTURE(waiting);
        RecordPagerFixture fixture;
        fixture.budget(waiting == "budget" ? 1 : 3);
        auto token = OSGBPagingToken::create();
        fixture.reads->makeNode = [observations = fixture.observations, group, waiting,
            state = std::weak_ptr<ReadState>(fixture.reads)](const vsg::Path& path, auto options) -> vsg::ref_ptr<vsg::Node> {
            if (path == "record-root.osgb") return recordedSource(observations, "coarse", "record-fine.osgb", options, 0.0, group);
            if (waiting == "compile")
            {
                auto gate = CompileGate::create(state.lock());
                gate->addChild(recordedLeaf(observations, "fine"));
                return gate;
            }
            return recordedLeaf(observations, "fine");
        };
        auto root = fixture.root("record-root.osgb", token);
        fixture.advance(root);
        fixture.pager->start(1);
        const bool rootReady = waitFor([&] { return fixture.pager->metrics().readyPages == 1; });
        REQUIRE(rootReady);
        fixture.update(fixture.frame);
        REQUIRE(root->children[0].node);
        CHECK(fixture.pager->residencyMetrics().residentGpuEstimatedBytes == recordedPageBytes);
        if (waiting == "read") { std::lock_guard lock(fixture.reads->mutex); fixture.reads->blocked = true; }
        fixture.advance(root);
        REQUIRE(fixture.observations->drew("coarse"));
        REQUIRE(fixture.pager->pagedLODContainer->activeList.count >= 2);
        const bool fineWaiting = waitFor([&] {
            if (waiting == "budget") return fixture.pager->residencyMetrics().budgetBlockedPages == 1;
            if (waiting == "read") return token->metrics().reading == 1;
            if (waiting == "compile") { std::lock_guard lock(fixture.reads->mutex); return fixture.reads->compileEntered; }
            return fixture.pager->metrics().readyPages == 1;
        });
        REQUIRE(fineWaiting);
        CHECK(fixture.pager->ownershipMetrics().containerPages == 2);
        CHECK(fixture.pager->ownershipMetrics().emptySlots == 1);
        CHECK_FALSE(fixture.observations->released("coarse"));
        CHECK_FALSE(fixture.observations->drew("fine"));
        token->close();
        root = {};
        fixture.advance(nullptr);
        const auto mergedAtClose = fixture.pager->metrics().merged;
        { std::lock_guard lock(fixture.reads->mutex); fixture.reads->released = true; fixture.reads->changed.notify_all(); }
        const bool released = waitFor([&] {
            fixture.advance(nullptr);
            return fixture.observations->allReleased() && fixture.pager->numActiveRequests == 0 &&
                fixture.pager->residencyMetrics().retiringPages == 0;
        });
        REQUIRE(released);
        CHECK(fixture.pager->pagedLODContainer->activeList.count == 0);
        CHECK(fixture.pager->pagedLODContainer->inactiveList.count == 0);
        const bool containerEmpty = std::none_of(fixture.pager->pagedLODContainer->elements.begin(), fixture.pager->pagedLODContainer->elements.end(),
            [](const auto& entry) { return bool(entry.plod); });
        CHECK(containerEmpty);
        const auto ownership = fixture.pager->ownershipMetrics();
        CHECK(ownership.containerPages == 0);
        CHECK(ownership.emptySlots == 0);
        CHECK(ownership.closedTokenPages == 0);
        CHECK(ownership.orphanPages == 0);
        CHECK(ownership.retiredTrackedObjects == 0);
        CHECK(ownership.subtreeRetirements > 0);
        CHECK(token->metrics().residentPages == 0);
        CHECK(token->metrics().ready == 0);
        CHECK(token->metrics().queued == 0);
        CHECK(fixture.pager->residencyMetrics().budgetBlockedPages == 0);
        CHECK(fixture.pager->residencyMetrics().retiringBytes == 0);
        CHECK(fixture.pager->residencyMetrics().residentCpuBytes == 0);
        CHECK(fixture.pager->residencyMetrics().readyCpuBytes == 0);
        CHECK(fixture.pager->residencyMetrics().decodedCpuBytes == 0);
        CHECK(fixture.pager->residencyMetrics().compilingCpuBytes == 0);
        CHECK(fixture.pager->residencyMetrics().readReservedBytes == 0);
        CHECK(fixture.pager->residencyMetrics().readClaimBytes == 0);
        CHECK(fixture.pager->metrics().merged == mergedAtClose);
    }
}

TEST_CASE("OSGB Record retires orphan descendants when the parent falls back or leaves the view", "[osgb-paging][memory][record]")
{
    for (const bool leaveView : {false, true})
    {
        CAPTURE(leaveView);
        RecordPagerFixture fixture;
        fixture.budget(8);
        auto token = OSGBPagingToken::create();
        fixture.reads->makeNode = [observations = fixture.observations](const vsg::Path& path, auto options) {
            if (path == "nested-root.osgb") return recordedSource(observations, "parent.coarse", "nested-middle.osgb", options, 0.0, true);
            if (path == "nested-middle.osgb") return recordedSource(observations, "middle.coarse", "nested-fine.osgb", options, 0.0, true);
            return recordedLeaf(observations, "nested.fine");
        };
        auto root = fixture.root("nested-root.osgb", token);
        fixture.pager->start(1);
        const bool refined = waitFor([&] { fixture.advance(root); return fixture.observations->drew("nested.fine"); });
        REQUIRE(refined);
        REQUIRE(token->metrics().residentPages == 3);
        OSGBPagingLimits paging;
        paging.residentBytesTarget = 1;
        paging.recentResidentSeconds = 0.0;
        paging.maxInactivePages = 0;
        fixture.pager->configure(paging);
        const bool retired = waitFor([&] {
            fixture.advance(root, leaveView ? 1000.0 : 0.0, 30.0);
            return fixture.observations->released("middle.coarse") && fixture.observations->released("middle.coarse.page") &&
                fixture.observations->released("nested.fine") && fixture.pager->residencyMetrics().retiringPages == 0;
        });
        REQUIRE(retired);
        CHECK(token->metrics().residentPages == (leaveView ? 0 : 1));
        CHECK(fixture.pager->residencyMetrics().residentCpuBytes == (leaveView ? 0 : recordedPageBytes));
        if (leaveView)
        {
            CHECK(fixture.observations->allReleased());
            CHECK(fixture.pager->pagedLODContainer->activeList.count == 0);
            CHECK(fixture.pager->pagedLODContainer->inactiveList.count == 0);
        }
        else
        {
            CHECK(fixture.observations->drew("parent.coarse"));
            CHECK_FALSE(fixture.observations->released("parent.coarse"));
            CHECK_FALSE(fixture.observations->drew("nested.fine"));
        }
        paging.residentBytesTarget = 8 * recordedPageBytes;
        fixture.pager->configure(paging);
        const bool returned = waitFor([&] { fixture.advance(root); return fixture.observations->drew("nested.fine"); });
        REQUIRE(returned);
        CHECK(token->metrics().residentPages == 3);
        CHECK(fixture.pager->residencyMetrics().retiringBytes == 0);
    }
}

TEST_CASE("OSGB Record closes one textured layer while the other continues rendering", "[osgb-paging][memory][record]")
{
    RecordPagerFixture fixture;
    fixture.budget(8);
    auto a = OSGBPagingToken::create(), b = OSGBPagingToken::create();
    fixture.reads->makeNode = [observations = fixture.observations](const vsg::Path& path, auto options) {
        if (path == "layer-a.osgb") return recordedSource(observations, "a.coarse", "a-fine.osgb", options, 0.0, true);
        if (path == "layer-b.osgb") return recordedSource(observations, "b.coarse", "b-fine.osgb", options, 2.0, true);
        return recordedLeaf(observations, path == "a-fine.osgb" ? "a.fine" : "b.fine", path == "a-fine.osgb" ? 0.0 : 2.0);
    };
    auto scene = vsg::Group::create();
    scene->addChild(fixture.root("layer-a.osgb", a));
    scene->addChild(fixture.root("layer-b.osgb", b, 2.0));
    fixture.pager->start(2);
    const bool both = waitFor([&] {
        fixture.advance(scene);
        return fixture.observations->drew("a.fine") && fixture.observations->drew("b.fine");
    });
    REQUIRE(both);
    a->close();
    scene->children.erase(scene->children.begin());
    const bool aReleased = waitFor([&] {
        fixture.advance(scene);
        return fixture.observations->released("a.coarse") && fixture.observations->released("a.coarse.page") &&
            fixture.observations->released("a.fine") && fixture.pager->residencyMetrics().retiringPages == 0;
    });
    REQUIRE(aReleased);
    CHECK(a->metrics().residentPages == 0);
    CHECK(b->metrics().residentPages == 2);
    CHECK(fixture.observations->drew("b.fine"));
    CHECK_FALSE(fixture.observations->released("b.coarse"));
    CHECK_FALSE(fixture.observations->released("b.fine"));
    CHECK(fixture.pager->residencyMetrics().residentCpuBytes == 2 * recordedPageBytes);
    b->close(); scene->children.clear();
    const bool allReleased = waitFor([&] {
        fixture.advance(scene);
        return fixture.observations->allReleased() && fixture.pager->residencyMetrics().retiringPages == 0;
    });
    REQUIRE(allReleased);
    CHECK(fixture.pager->pagedLODContainer->activeList.count == 0);
    CHECK(fixture.pager->pagedLODContainer->inactiveList.count == 0);
}

TEST_CASE("OSGB Record releases descendants after the native page count limit evicts their parent", "[osgb-paging][memory][record]")
{
    RecordPagerFixture fixture;
    fixture.budget(8);
    auto token = OSGBPagingToken::create();
    fixture.reads->makeNode = [observations = fixture.observations](const vsg::Path& path, auto options) {
        if (path == "count-root.osgb") return recordedSource(observations, "count.parent", "count-middle.osgb", options, 0.0, true);
        if (path == "count-middle.osgb") return recordedSource(observations, "count.middle", "count-fine.osgb", options, 0.0, true);
        return recordedLeaf(observations, "count.fine");
    };
    auto root = fixture.root("count-root.osgb", token);
    fixture.pager->start(1);
    const bool refined = waitFor([&] { fixture.advance(root); return fixture.observations->drew("count.fine"); });
    REQUIRE(refined);
    REQUIRE(token->metrics().residentPages == 3);
    REQUIRE(fixture.pager->pagedLODContainer->activeList.count == 3);
    REQUIRE(fixture.pager->metrics().residentEstimatedBytes == 3 * recordedPageBytes);
    // SDK 字节目标、期限和非活动数量均不触发淘汰；只让原生容器从三个页减到两个。
    fixture.pager->targetMaxNumPagedLODWithHighResSubgraphs = 2;
    const bool parentDetached = waitFor([&] {
        fixture.advance(root, 1000.0);
        return !root->children[0].node && !root->pending;
    });
    REQUIRE(parentDetached);
    CHECK_FALSE(token->closed());
    CHECK(root->index == 0);
    const bool descendantsReleased = waitFor([&] {
        fixture.advance(root, 1000.0);
        return fixture.observations->allReleased() && fixture.pager->residencyMetrics().retiringPages == 0;
    });
    REQUIRE(descendantsReleased);
    CHECK(fixture.observations->released("count.parent.group"));
    CHECK(fixture.observations->released("count.parent.page"));
    CHECK(fixture.observations->released("count.middle.group"));
    CHECK(fixture.observations->released("count.middle.page"));
    CHECK(token->metrics().residentPages == 0);
    CHECK(fixture.pager->residencyMetrics().residentCpuBytes == 0);
    CHECK(fixture.pager->residencyMetrics().retiringBytes == 0);
    const bool containerEmpty = std::none_of(fixture.pager->pagedLODContainer->elements.begin(), fixture.pager->pagedLODContainer->elements.end(),
        [](const auto& entry) { return bool(entry.plod); });
    CHECK(containerEmpty);
    fixture.pager->targetMaxNumPagedLODWithHighResSubgraphs = 8;
    const bool returned = waitFor([&] { fixture.advance(root); return fixture.observations->drew("count.fine"); });
    REQUIRE(returned);
    CHECK(token->metrics().residentPages == 3);
}

TEST_CASE("OSGB Record preserves visible coarse coverage while panning A to B and back under a fixed budget", "[osgb-paging][memory][record]")
{
    RecordPagerFixture fixture;
    fixture.budget(2);
    auto token = OSGBPagingToken::create();
    fixture.reads->makeNode = [observations = fixture.observations](const vsg::Path& path, auto options) {
        if (path == "area-a.osgb") return recordedSource(observations, "area-a.coarse", "area-a-fine.osgb", options);
        if (path == "area-b.osgb") return recordedSource(observations, "area-b.coarse", "area-b-fine.osgb", options, 10.0);
        return recordedLeaf(observations, path == "area-a-fine.osgb" ? "area-a.fine" : "area-b.fine",
            path == "area-a-fine.osgb" ? 0.0 : 10.0);
    };
    auto scene = vsg::Group::create();
    auto a = fixture.root("area-a.osgb", token), b = fixture.root("area-b.osgb", token, 10.0);
    scene->addChild(a); scene->addChild(b);
    fixture.pager->start(1);
    const bool first = waitFor([&] { fixture.advance(scene); return fixture.observations->drew("area-a.fine"); });
    REQUIRE(first);
    REQUIRE_FALSE(b->children[0].node);
    REQUIRE(fixture.pager->residencyMetrics().residentCpuBytes == 2 * recordedPageBytes);
    const auto beforeCoverage = fixture.pager->metrics().merged;
    bool missingA = false;
    const bool covered = waitFor([&] {
        fixture.advance(scene, 5.0);
        missingA |= !fixture.observations->drew("area-a.coarse") && !fixture.observations->drew("area-a.fine");
        return fixture.observations->drew("area-a.coarse") && fixture.observations->drew("area-b.coarse");
    });
    REQUIRE(covered);
    CHECK(fixture.pager->ownershipMetrics().coveragePreemptions > 0);
    CHECK_FALSE(missingA);
    CHECK(b->children[0].node);
    CHECK(fixture.pager->metrics().merged == beforeCoverage + 1);
    CHECK(fixture.pager->residencyMetrics().residentCpuBytes == 2 * recordedPageBytes);
    CHECK(fixture.pager->memoryMetrics().lodScale == 1.0);
    const bool refinedB = waitFor([&] { fixture.advance(scene, 10.0); return fixture.observations->drew("area-b.fine"); });
    REQUIRE(refinedB);
    CHECK(fixture.observations->released("area-a.coarse"));
    CHECK(fixture.observations->released("area-a.fine"));
    CHECK_FALSE(a->children[0].node);
    const bool returned = waitFor([&] { fixture.advance(scene); return fixture.observations->drew("area-a.fine"); });
    REQUIRE(returned);
    CHECK(fixture.observations->released("area-b.coarse"));
    CHECK(fixture.observations->released("area-b.fine"));
    CHECK_FALSE(b->children[0].node);
    CHECK(fixture.pager->residencyMetrics().residentCpuBytes == 2 * recordedPageBytes);
    token->close(); scene->children.clear(); a = {}; b = {};
    const bool released = waitFor([&] {
        fixture.advance(nullptr);
        return fixture.observations->allReleased() && fixture.pager->residencyMetrics().retiringPages == 0;
    });
    REQUIRE(released);
    CHECK(fixture.pager->pagedLODContainer->activeList.count == 0);
    CHECK(fixture.pager->pagedLODContainer->inactiveList.count == 0);
    CHECK(fixture.pager->residencyMetrics().retiringBytes == 0);
}

TEST_CASE("OSGB ready backpressure bounds work and does not lose the next page", "[osgb-paging]")
{
    PagerFixture fixture;
    OSGBPagingLimits limits;
    limits.maxReadyPages = 1;
    limits.maxMergesPerFrame = 1;
    fixture.pager->configure(limits);
    auto first = fixture.page("first.osgb"), second = fixture.page("second.osgb");
    fixture.pager->request(first); fixture.pager->request(second);
    fixture.pager->start(2);
    const bool ready = waitFor([&] { return fixture.pager->metrics().readyPages == 1; });
    REQUIRE(ready);
    CHECK(fixture.pager->compileManager->successfulCompileCount == 1);
    REQUIRE(fixture.update());
    const bool next = waitFor([&] { return fixture.pager->metrics().readyPages == 1; });
    REQUIRE(next);
    REQUIRE(fixture.update());
    CHECK(first->children[0].node);
    CHECK(second->children[0].node);
    CHECK(fixture.pager->numActiveRequests == 0);
    CHECK(fixture.pager->metrics().compilingCpuBytes == 0);
}

TEST_CASE("OSGB failed generic read preserves fallback and returns to retry state", "[osgb-paging]")
{
    PagerFixture fixture;
    auto missing = fixture.page("missing.osgb");
    auto coarse = vsg::Group::create();
    missing->children[1].node = coarse;
    fixture.pager->request(missing);
    fixture.pager->start(1);
    const bool failed = waitFor([&] { return fixture.pager->metrics().failed == 1; });
    REQUIRE(failed);
    CHECK(missing->children[1].node == coarse);
    CHECK_FALSE(missing->children[0].node);
    CHECK(missing->requestStatus == vsg::PagedLOD::NoRequest);
    CHECK(missing->requestCount == 0);
    CHECK(missing->frameNextLoadAttempt == 10 + fixture.pager->delayBeforeNextLoadAttempt);
    CHECK(fixture.pager->numActiveRequests == 0);
}

TEST_CASE("OSGB close retires middle head and tail without corrupting the native container", "[osgb-paging]")
{
    PagerFixture fixture;
    std::vector<vsg::ref_ptr<vsg::PagedLOD>> pages;
    std::vector<vsg::ref_ptr<OSGBPagingToken>> tokens;
    for (const auto* name : {"a.osgb", "b.osgb", "c.osgb"})
    {
        auto token = OSGBPagingToken::create();
        auto page = fixture.page(name);
        page->options->setObject(osgbPagingTokenKey, token);
        tokens.push_back(token); pages.push_back(page);
        fixture.pager->request(page);
    }
    fixture.pager->start(1);
    const bool ready = waitFor([&] { return fixture.pager->metrics().readyPages == 3; });
    REQUIRE(ready);
    fixture.update();
    // 软时间预算可能分帧提交，因此明确排空后再核对全部链表项。
    while (fixture.pager->metrics().readyPages) fixture.update();
    auto container = fixture.pager->pagedLODContainer;
    CHECK(container->activeList.count == 3);
    auto checkLists = [&] {
        std::set<uint32_t> all;
        for (auto* list : {&container->activeList, &container->inactiveList, &container->availableList})
        {
            uint32_t previous = 0, count = 0;
            for (uint32_t i = list->head; i; i = container->elements[i].next)
            {
                REQUIRE(i < container->elements.size());
                REQUIRE(all.insert(i).second);
                CHECK(container->elements[i].previous == previous);
                CHECK(container->elements[i].list == list);
                previous = i; ++count;
            }
            CHECK(previous == list->tail);
            CHECK(count == list->count);
        }
        CHECK(all.size() + 1 == container->elements.size());
    };
    checkLists();
    tokens[1]->close(); fixture.update();
    CHECK(pages[1]->index == 0);
    CHECK(container->activeList.count == 2);
    checkLists();
    tokens[0]->close(); fixture.update();
    CHECK(container->activeList.count == 1);
    checkLists();
    tokens[2]->close(); fixture.update(); fixture.update();
    CHECK(container->activeList.count == 0);
    CHECK(container->inactiveList.count == 0);
    CHECK(fixture.pager->metrics().residentEstimatedBytes == 0);
    checkLists();
}

TEST_CASE("OSGB untrusted index bounds do not cull before the real root arrives", "[osgb-paging]")
{
    PagerFixture fixture;
    auto root = fixture.page("unknown-z.osgb", true).cast<OSGBRoot>();
    root->bound = vsg::dsphere(1000000.0, 1000000.0, 1000000.0, 1.0);
    root->requestCount = 0;
    auto traversal = vsg::RecordTraversal::create();
    traversal->state->setProjectionAndViewMatrix(vsg::dmat4{}, vsg::dmat4{});
    auto stamp = vsg::FrameStamp::create();
    stamp->frameCount = 10;
    traversal->setFrameStamp(stamp);
    traversal->setDatabasePager(fixture.pager);
    root->accept(*traversal);
    CHECK(fixture.pager->metrics().queuedRoots == 0);
    root->boundTrusted = false;
    CHECK_FALSE(root->clone().cast<OSGBRoot>()->boundTrusted);
    root->accept(*traversal);
    CHECK(fixture.pager->metrics().queuedRoots == 1);
    CHECK(std::isfinite(root->priority.load()));
    fixture.pager->start(1);
    const bool ready = waitFor([&] { return fixture.pager->metrics().readyPages == 1; });
    REQUIRE(ready);
    REQUIRE(fixture.update());
    CHECK(root->boundTrusted);
    CHECK(root->bound.x == Approx(0.0));
    CHECK(root->bound.y == Approx(0.0));
    CHECK(root->bound.z == Approx(0.0));
    CHECK(root->bound.r > 0.0);
}

TEST_CASE("OSGB token counters follow real queued ready resident and closed states", "[osgb-paging]")
{
    PagerFixture fixture;
    OSGBPagingLimits limits;
    limits.maxMergesPerFrame = 1;
    fixture.pager->configure(limits);
    auto token = OSGBPagingToken::create();
    fixture.options->setObject(osgbPagingTokenKey, token);
    auto root = fixture.page("metrics-root.osgb", true);
    auto detail = fixture.page("metrics-detail.osgb");
    fixture.pager->request(root); fixture.pager->request(detail);
    CHECK(token->metrics().queued == 2);
    fixture.pager->start(1);
    const bool rootReady = waitFor([&] { return fixture.pager->metrics().readyPages == 1; });
    REQUIRE(rootReady);
    auto stats = token->metrics();
    CHECK(stats.queued == 1);
    CHECK(stats.reading == 0);
    CHECK(stats.compiling == 0);
    CHECK(stats.ready == 1);
    CHECK(stats.residentPages == 0);
    CHECK(stats.merged == 0);
    REQUIRE(fixture.update());
    const bool detailReady = waitFor([&] { return fixture.pager->metrics().readyPages == 1; });
    REQUIRE(detailReady);
    stats = token->metrics();
    CHECK(stats.queued == 0);
    CHECK(stats.reading == 0);
    CHECK(stats.compiling == 0);
    CHECK(stats.ready == 1);
    CHECK(stats.residentPages == 1);
    CHECK(stats.residentRoots == 1);
    CHECK(stats.residentDraws == 1);
    CHECK(stats.residentBytes == 3 * sizeof(vsg::vec3));
    CHECK(stats.merged == 1);
    REQUIRE(fixture.update());
    stats = token->metrics();
    CHECK(stats.ready == 0);
    CHECK(stats.residentPages == 2);
    CHECK(stats.residentRoots == 1);
    CHECK(stats.residentDraws == 2);
    CHECK(stats.residentBytes == 6 * sizeof(vsg::vec3));
    CHECK(stats.merged == 2);
    CHECK(stats.queueWaitMs >= 0.0);
    CHECK(stats.readMs > 0.0);
    CHECK(stats.compileMs >= 0.0);
    CHECK(stats.mergeMs > 0.0);
    CHECK(fixture.pager->metrics().readMs == Approx(stats.readMs));
    token->close(); fixture.update();
    stats = token->metrics();
    CHECK(stats.residentPages == 0);
    CHECK(stats.residentRoots == 0);
    CHECK(stats.residentDraws == 0);
    CHECK(stats.residentBytes == 0);
    auto rejected = fixture.page("closed.osgb");
    fixture.pager->request(rejected);
    CHECK(token->metrics().rejected == 1);
    CHECK(token->metrics().queued == 0);
}

TEST_CASE("OSGB recent resident limits only retire inactive pages", "[osgb-paging]")
{
    PagerFixture fixture;
    auto token = OSGBPagingToken::create();
    fixture.options->setObject(osgbPagingTokenKey, token);
    OSGBPagingLimits limits;
    limits.maxInactivePages = 1;
    limits.recentResidentSeconds = 0.0;
    fixture.pager->configure(limits);
    for (const char* name : {"recent-a.osgb", "recent-b.osgb", "recent-c.osgb"})
        fixture.pager->request(fixture.page(name, true));
    fixture.pager->start(1);
    const bool ready = waitFor([&] { return fixture.pager->metrics().readyPages == 3; });
    REQUIRE(ready);
    while (fixture.pager->metrics().readyPages) fixture.update();
    CHECK(token->metrics().residentPages == 3);
    fixture.update(20);
    CHECK(token->metrics().residentPages == 1);
    CHECK(fixture.pager->pagedLODContainer->inactiveList.count == 1);
    limits.recentResidentSeconds = 0.001;
    fixture.pager->configure(limits);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    fixture.update(20);
    CHECK(token->metrics().residentPages == 0);
    CHECK(fixture.pager->metrics().residentEstimatedBytes == 0);
}

TEST_CASE("OSGB LOD thresholds follow viewport and FOV without changing generic pages", "[osgb-paging]")
{
    auto group = vsg::StateGroup::create();
    auto pixel = vsg::PagedLOD::create(), distance = vsg::PagedLOD::create(), generic = vsg::PagedLOD::create();
    pixel->setValue("osgb.pixelSplit", 540.0);
    distance->setValue("osgb.distanceSplit", 100.0);
    distance->bound = vsg::dsphere(0.0, 0.0, 0.0, 10.0);
    generic->children[0].minimumScreenHeightRatio = 0.123;
    pixel->children[1].minimumScreenHeightRatio = 0.9;
    group->addChild(pixel); group->addChild(distance); group->addChild(generic);
    adjustOSGBLOD(*group, 1080.0, 2.0);
    CHECK(pixel->children[0].minimumScreenHeightRatio == Approx(0.5));
    CHECK(distance->children[0].minimumScreenHeightRatio == Approx(20.0 / (std::sqrt(2.0) * 100.0)));
    CHECK(pixel->children[1].minimumScreenHeightRatio == Approx(0.0));
    CHECK(generic->children[0].minimumScreenHeightRatio == Approx(0.123));
    adjustOSGBLOD(*group, 2160.0, -4.0);
    CHECK(pixel->children[0].minimumScreenHeightRatio == Approx(0.25));
    CHECK(distance->children[0].minimumScreenHeightRatio == Approx(40.0 / (std::sqrt(2.0) * 100.0)));
    adjustOSGBLOD(*group, 0.0, 4.0);
    CHECK(pixel->children[0].minimumScreenHeightRatio == Approx(0.25));

    PagerFixture fixture;
    fixture.reads->emitPaged = true;
    auto token = OSGBPagingToken::create();
    fixture.options->setObject(osgbPagingTokenKey, token);
    auto root = fixture.page("late-viewport.osgb", true);
    fixture.pager->request(root);
    fixture.pager->start(1);
    const bool ready = waitFor([&] { return fixture.pager->metrics().readyPages == 1; });
    REQUIRE(ready);
    token->viewportHeight = 2160.0;
    REQUIRE(fixture.update());
    auto merged = root->children[0].node.cast<vsg::Group>();
    REQUIRE(merged);
    REQUIRE(merged->children.size() == 2);
    auto child = merged->children[1].cast<vsg::PagedLOD>();
    REQUIRE(child);
    CHECK(child->children[0].minimumScreenHeightRatio == Approx(0.25));
    CHECK(child->children[1].minimumScreenHeightRatio == Approx(0.0));
}
