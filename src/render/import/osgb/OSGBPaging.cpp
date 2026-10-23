#include "OSGBPaging.h"
#include <vsg/app/Viewer.h>
#include <vsg/app/RecordAndSubmitTask.h>
#include <vsg/core/Data.h>
#include <vsg/io/read.h>
#include <vsg/nodes/VertexDraw.h>
#include <vsg/nodes/VertexIndexDraw.h>
#include <vsg/commands/Draw.h>
#include <vsg/commands/DrawIndexed.h>
#include <vsg/state/Image.h>
#include <vsg/state/ImageView.h>
#include <vsg/state/BufferInfo.h>
#include <vsg/core/Auxiliary.h>
#include <vsg/ui/FrameStamp.h>
#include <vsg/utils/ComputeBounds.h>
#include <vsg/vk/State.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <limits>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <vector>

namespace geostudio::native::io
{
namespace
{
using Clock = std::chrono::steady_clock;
OSGBPagingToken* pagingToken(const vsg::PagedLOD& page)
{
    return page.options ? page.options->getObject<OSGBPagingToken>(osgbPagingTokenKey) : nullptr;
}
bool valid(const vsg::PagedLOD& page)
{
    const auto token = pagingToken(page);
    return !token || !token->closed();
}
bool recent(const vsg::PagedLOD& page, uint64_t frame)
{
    const auto used = page.frameHighResLastUsed.load();
    return frame <= used || frame - used <= 1;
}
double finitePriority(double value)
{
    return std::isfinite(value) ? std::clamp(value, 0.0, 1000000.0) : 1000000.0;
}
// 只观察析构断开标记，不把零引用节点重新提升为强引用，避免与 DeleteQueue 并发析构竞争。
struct NodeLifetime
{
    vsg::ref_ptr<vsg::Auxiliary> auxiliary;
    NodeLifetime() = default;
    explicit NodeLifetime(vsg::Object* node) : auxiliary(node ? node->getOrCreateAuxiliary() : nullptr) {}
    bool alive() const
    {
        if (!auxiliary) return false;
        std::lock_guard lock(auxiliary->getMutex());
        return auxiliary->getConnectedObject() != nullptr;
    }
};
// 仅持有本页上传对象；Update 在 CPU 上传复制完成后解除静态载荷引用。
struct UploadData
{
    std::vector<vsg::ref_ptr<vsg::BufferInfo>> buffers;
    std::vector<vsg::ref_ptr<vsg::ImageInfo>> images;
    uint64_t bytes() const
    {
        std::set<const vsg::Data*> seen;
        uint64_t result = 0;
        auto add = [&](const vsg::Data* data) { if (data && seen.insert(data).second) result += data->dataSize(); };
        for (const auto& buffer : buffers) add(buffer->data);
        for (const auto& image : images) add(image->imageView->image->data);
        return result;
    }
    void release(uint32_t deviceID)
    {
        for (const auto& buffer : buffers)
            if (buffer->data && !buffer->data->dynamic() && buffer->buffer &&
                buffer->buffer->getDeviceMemory(deviceID) && buffer->copiedModifiedCounts[deviceID].count &&
                !buffer->requiresCopy(deviceID)) buffer->data = {};
        // TransferTask 同步 ImageInfo 的版本；同一 Image 的全部引用上传后才释放。
        std::map<vsg::Image*, bool> uploaded;
        for (const auto& info : images)
        {
            auto* image = info->imageView->image.get();
            const bool complete = image->data && !image->data->dynamic() && image->getDeviceMemory(deviceID) &&
                info->copiedModifiedCounts[deviceID].count && !info->requiresCopy(deviceID);
            auto [it, inserted] = uploaded.emplace(image, complete);
            if (!inserted) it->second = it->second && complete;
        }
        for (const auto& [image, complete] : uploaded) if (complete) image->data = {};
        buffers.erase(std::remove_if(buffers.begin(), buffers.end(), [](const auto& b) { return !b->data; }), buffers.end());
        images.erase(std::remove_if(images.begin(), images.end(), [](const auto& i) { return !i->imageView->image->data; }), images.end());
    }
};
struct PageWatch
{
    vsg::PagedLOD* page = nullptr;
    NodeLifetime lifetime;
};
struct Footprint
{
    uint64_t bytes = 0, draws = 0;
    std::shared_ptr<UploadData> upload;
    std::vector<NodeLifetime> observed;
    std::vector<PageWatch> nestedPages;
};
Footprint footprint(const vsg::Node& node)
{
    // 只统计该独立子图里的 Data；不遍历工作线程之外的可变分页图。
    struct Count : vsg::CollectResourceRequirements
    {
        std::set<const vsg::Data*> seen;
        uint64_t bytes = 0;
        uint64_t draws = 0;
        std::shared_ptr<UploadData> upload = std::make_shared<UploadData>();
        std::vector<NodeLifetime> observed;
        std::vector<PageWatch> nestedPages;
        std::set<const vsg::Object*> watched;
        void observe(const vsg::Object* object)
        {
            if (object && watched.insert(object).second) observed.emplace_back(const_cast<vsg::Object*>(object));
        }
        void apply(const vsg::PagedLOD& page) override
        {
            if (pagingToken(page)) nestedPages.push_back({const_cast<vsg::PagedLOD*>(&page), NodeLifetime(const_cast<vsg::PagedLOD*>(&page))});
            vsg::CollectResourceRequirements::apply(page);
        }
        void add(const vsg::Data* data)
        {
            if (data && seen.insert(data).second)
            {
                bytes += std::min<uint64_t>(data->dataSize(), std::numeric_limits<uint64_t>::max() - bytes);
                observe(data);
            }
        }
        void apply(vsg::ref_ptr<vsg::BufferInfo> buffer, BufferProperties) override
        {
            if (buffer) { add(buffer->data); observe(buffer); upload->buffers.push_back(buffer); }
        }
        void apply(vsg::ref_ptr<vsg::ImageInfo> image) override
        {
            if (image && image->imageView && image->imageView->image)
            { add(image->imageView->image->data); observe(image); upload->images.push_back(image); }
        }
        void apply(const vsg::VertexDraw& draw) override
        { ++draws; vsg::CollectResourceRequirements::apply(draw); }
        void apply(const vsg::VertexIndexDraw& draw) override
        { ++draws; vsg::CollectResourceRequirements::apply(draw); }
        void apply(const vsg::Draw&) override { ++draws; }
        void apply(const vsg::DrawIndexed&) override { ++draws; }
    } count;
    node.accept(count);
    return {count.bytes, count.draws, std::move(count.upload), std::move(count.observed), std::move(count.nestedPages)};
}

// 可退让的精模必须仍有可绘制粗模；空 Group 或只有外部路径的占位不算覆盖。
bool drawableFallback(const vsg::Node& node)
{
    struct Find : vsg::ConstVisitor
    {
        bool found = false;
        void apply(const vsg::Node& node) override { if (!found) node.traverse(*this); }
        void apply(const vsg::PagedLOD& page) override
        {
            const auto& child = page.children[1].node ? page.children[1].node : page.children[0].node;
            if (!found && child) child->accept(*this);
        }
        void apply(const vsg::VertexDraw&) override { found = true; }
        void apply(const vsg::VertexIndexDraw&) override { found = true; }
        void apply(const vsg::Draw&) override { found = true; }
        void apply(const vsg::DrawIndexed&) override { found = true; }
    } visitor;
    node.accept(visitor);
    return visitor.found;
}

// 固定 VSG 的容器成员函数未导出；仅在 Update 中按其公开链表契约摘除指定页。
// 调用者持有 page，退休对象随后交给 DeleteQueue；空索引或失配时不改容器。
void detachPage(vsg::PagedLODContainer& container, vsg::PagedLOD& page)
{
    const uint32_t index = page.index;
    if (!index || index >= container.elements.size()) return;
    auto& entry = container.elements[index];
    if (entry.plod.get() != &page || !entry.list || entry.list == &container.availableList) return;
    auto& source = *entry.list;
    if (entry.previous) container.elements[entry.previous].next = entry.next;
    else source.head = entry.next;
    if (entry.next) container.elements[entry.next].previous = entry.previous;
    else source.tail = entry.previous;
    --source.count;
    auto& available = container.availableList;
    entry.previous = available.tail;
    entry.next = 0;
    entry.list = &available;
    if (available.tail) container.elements[available.tail].next = index;
    else available.head = index;
    available.tail = index;
    ++available.count;
    page.index = 0;
    entry.plod = {};
}
}

OSGBLayerPagingMetrics OSGBPagingToken::metrics() const
{
    std::lock_guard lock(_metricsMutex);
    return _metrics;
}
void adjustOSGBLOD(vsg::Node& node, double height, double projectionY)
{
    if (!std::isfinite(height) || height <= 0.0 || !std::isfinite(projectionY) || projectionY == 0.0) return;
    struct Adjust : vsg::Visitor
    {
        double height, projectionY;
        Adjust(double h, double p) : height(h), projectionY(p) {}
        void apply(vsg::Node& node) override { node.traverse(*this); }
        // StateGroup 走 Group 的 children，避免进入描述符/纹理状态。
        void apply(vsg::Group& group) override
        { for (auto& child : group.children) if (child) child->accept(*this); }
        void apply(vsg::PagedLOD& page) override
        {
            double split = 0.0, threshold = 0.0;
            bool adjusted = false;
            if (page.getValue("osgb.pixelSplit", split) && std::isfinite(split) && split > 0.0)
            { threshold = split / height; adjusted = true; }
            else if (page.getValue("osgb.distanceSplit", split) && std::isfinite(split) && split > 0.0 &&
                     std::isfinite(page.bound.r) && page.bound.r > 0.0)
            { threshold = page.bound.r * std::abs(projectionY) / (std::sqrt(2.0) * split); adjusted = true; }
            if (adjusted && std::isfinite(threshold))
            {
                page.children[0].minimumScreenHeightRatio = threshold;
                page.children[1].minimumScreenHeightRatio = 0.0;
            }
            page.traverse(*this);
        }
    } visitor(height, projectionY);
    node.accept(visitor);
}

OSGBRoot::OSGBRoot(const OSGBRoot& rhs, const vsg::CopyOp& copy) : Inherit(rhs, copy), boundTrusted(rhs.boundTrusted) {}
vsg::ref_ptr<vsg::Object> OSGBRoot::clone(const vsg::CopyOp& copy) const { return OSGBRoot::create(*this, copy); }
void OSGBRoot::accept(vsg::Visitor& visitor) { visitor.apply(static_cast<vsg::PagedLOD&>(*this)); }
void OSGBRoot::accept(vsg::ConstVisitor& visitor) const { visitor.apply(static_cast<const vsg::PagedLOD&>(*this)); }
void OSGBRoot::read(vsg::Input&) { throw std::runtime_error("OSGBRoot is a runtime index node, not a serialized page"); }
void OSGBRoot::write(vsg::Output&) const { throw std::runtime_error("OSGBRoot is a runtime index node, not a serialized page"); }
double OSGBRoot::projectedPriority(double radius, double distance) noexcept
{
    if (!std::isfinite(radius) || radius <= 0.0 || !std::isfinite(distance)) return 0.0;
    return finitePriority(radius / std::max(1e-9, std::abs(distance)));
}
void OSGBRoot::accept(vsg::RecordTraversal& visitor) const
{
    if (!visitor.state || !visitor.frameStamp || !valid(*this)) return;
    const auto frame = visitor.frameStamp->frameCount;
    double distance = 0.0;
    if (boundTrusted) distance = visitor.state->lodDistance(bound);
    else
    {
        const auto eye = visitor.state->modelviewMatrixStack.top() * vsg::dvec3(bound.x, bound.y, bound.z);
        distance = vsg::length(eye);
    }
    if (boundTrusted && distance < 0.0)
    {
        if (visitor.culledPagedLODs && !recent(*this, frame))
            visitor.culledPagedLODs->highresCulled.emplace_back(this);
        return;
    }
    const auto previous = frameHighResLastUsed.exchange(frame);
    if (visitor.culledPagedLODs && frame > previous && frame - previous > 1)
        visitor.culledPagedLODs->newHighresRequired.emplace_back(this);
    if (children[0].node)
    {
        children[0].node->accept(visitor);
        return;
    }
    priority.store(projectedPriority(bound.r, distance));
    if (visitor.databasePager)
    {
        // 自有 Pager 用请求状态统一准入；固定原生 Pager 仍需要其旧计数门控。
        if (dynamic_cast<OSGBDatabasePager*>(visitor.databasePager.get()) || requestCount.fetch_add(1) == 0)
            visitor.databasePager->request(vsg::ref_ptr<vsg::PagedLOD>(const_cast<OSGBRoot*>(this)));
    }
    if (children[1].node) children[1].node->accept(visitor);
}

struct OSGBDatabasePager::Impl
{
    struct Request
    {
        vsg::ref_ptr<vsg::PagedLOD> page;
        Clock::time_point queued;
        uint64_t requiredBytes = 0;
        bool budgetBlocked = false, oversized = false;
    };
    struct Ready { vsg::ref_ptr<vsg::PagedLOD> page; vsg::CompileResult result; Footprint resources; };
    struct Resident
    {
        Footprint resources;
        vsg::ref_ptr<OSGBPagingToken> token;
        bool root = false;
        Clock::time_point lastActive;
        NodeLifetime node, wrapper;
    };
    mutable std::mutex mutex;
    std::condition_variable changed;
    OSGBPagingLimits limits;
    OSGBPagingMetrics totals;
    OSGBMemoryLimits memoryLimits;
    OSGBMemoryMetrics memory;
    OSGBResidencyLimits residencyLimits;
    OSGBPagingOwnershipMetrics ownership;
    struct Suppression { NodeLifetime node; uint64_t untilFrame = 0; };
    std::map<vsg::PagedLOD*, Suppression> abandoned, cooldown;
    bool permitted(const vsg::PagedLOD& page) const
    {
        const auto found = abandoned.find(const_cast<vsg::PagedLOD*>(&page));
        return valid(page) && (found == abandoned.end() || !found->second.node.alive());
    }
    bool cooling(const vsg::PagedLOD& page, uint64_t frame) const
    {
        const auto found = cooldown.find(const_cast<vsg::PagedLOD*>(&page));
        return found != cooldown.end() && found->second.node.alive() && frame < found->second.untilFrame;
    }
    bool coveragePending(uint64_t frame) const
    {
        for (const auto& request : roots)
            if (pagingToken(*request.page) && permitted(*request.page) && recent(*request.page, frame) &&
                request.requiredBytes <= admissionLimit(true) && request.requiredBytes <= memoryLimits.readWorkingBytes) return true;
        // 在途根已持有 readClaim/decoded 预算，不因过期的后台读取阻塞全部细化。
        for (const auto& item : ready)
            if (pagingToken(*item.page) && dynamic_cast<OSGBRoot*>(item.page.get()) && permitted(*item.page) && recent(*item.page, frame)) return true;
        return false;
    }
    uint64_t readClaims = 0, compilingBytes = 0, scheduledBytes = 0;
    uint64_t transitionLimit() const { return std::min(residencyLimits.transitionBytes, memoryLimits.residentBytes / 4); }
    uint64_t detailReserve() const { return std::min(residencyLimits.detailReserveBytes, memoryLimits.residentBytes / 8); }
    uint64_t admissionLimit(bool root, uint64_t = 0) const
    {
        const auto total = memoryLimits.residentBytes + transitionLimit();
        return root ? total : total - detailReserve();
    }
    uint64_t heldBytes() const
    { return totals.residentEstimatedBytes + readyBytes + reservedBytes + readClaims + memory.retiringBytes; }
    struct Retired
    {
        NodeLifetime node;
        uint64_t bytes = 0;
        std::vector<NodeLifetime> resources;
        bool alive() const
        {
            if (node.alive()) return true;
            for (const auto& resource : resources) if (resource.alive()) return true;
            return false;
        }
    };
    std::vector<Retired> retired;
    std::deque<Request> roots, details;
    std::deque<Ready> ready;
    std::map<vsg::PagedLOD*, Resident> resident;
    uint64_t readyBytes = 0, reservedBytes = 0, reservedPages = 0, activeRoots = 0, activeDetails = 0;
    bool started = false, preferRoot = true;
};

OSGBDatabasePager::OSGBDatabasePager() : _impl(new Impl) {}
OSGBDatabasePager::~OSGBDatabasePager() { shutdown(); }
void OSGBDatabasePager::configureMemory(const OSGBMemoryLimits& limits)
{
    if (!limits.maxPageBytes || limits.maxPageBytes > limits.residentBytes ||
        limits.maxPageBytes > limits.readReservationBytes || limits.readReservationBytes > limits.readWorkingBytes)
        throw std::invalid_argument("OSGB memory limits must admit one bounded page and read");
    std::lock_guard lock(_impl->mutex);
    _impl->memoryLimits = limits;
    _impl->changed.notify_all();
}
OSGBMemoryMetrics OSGBDatabasePager::memoryMetrics() const
{
    std::lock_guard lock(_impl->mutex);
    auto value = _impl->memory;
    value.residentLimit = _impl->memoryLimits.residentBytes;
    value.readWorkingLimit = _impl->memoryLimits.readWorkingBytes;
    return value;
}
void OSGBDatabasePager::configureResidency(const OSGBResidencyLimits& limits)
{
    std::lock_guard lock(_impl->mutex);
    _impl->residencyLimits = limits;
    _impl->changed.notify_all();
}
OSGBResidencyMetrics OSGBDatabasePager::residencyMetrics() const
{
    std::lock_guard lock(_impl->mutex);
    OSGBResidencyMetrics result;
    result.residentPages = _impl->resident.size();
    result.residentGpuEstimatedBytes = _impl->totals.residentEstimatedBytes;
    for (const auto& [page, resident] : _impl->resident)
        if (resident.resources.upload) result.residentCpuBytes += resident.resources.upload->bytes();
    result.compilingCpuBytes = _impl->compilingBytes;
    result.decodedCpuBytes = _impl->reservedBytes - _impl->compilingBytes;
    result.readyCpuBytes = _impl->readyBytes;
    result.readReservedBytes = _impl->memory.readReservedBytes;
    result.readClaimBytes = _impl->readClaims;
    result.retiringPages = _impl->retired.size();
    result.retiringBytes = _impl->memory.retiringBytes;
    for (auto* queue : {&_impl->roots, &_impl->details})
        for (const auto& request : *queue)
        {
            result.budgetBlockedPages += request.budgetBlocked ? 1 : 0;
            result.oversizedWaitingPages += request.oversized ? 1 : 0;
        }
    result.transitionLimit = _impl->transitionLimit();
    result.detailReserveBytes = _impl->detailReserve();
    return result;
}
OSGBPagingOwnershipMetrics OSGBDatabasePager::ownershipMetrics() const
{
    std::lock_guard lock(_impl->mutex);
    auto result = _impl->ownership;
    result.queuedCoverage = result.coverageBlockedPages = 0;
    for (const auto& request : _impl->roots)
        if (pagingToken(*request.page))
        {
            ++result.queuedCoverage;
            if (request.budgetBlocked) ++result.coverageBlockedPages;
        }
    result.retiredTrackedObjects = 0;
    for (const auto& retired : _impl->retired)
    {
        if (retired.node.alive()) ++result.retiredTrackedObjects;
        for (const auto& observed : retired.resources) if (observed.alive()) ++result.retiredTrackedObjects;
    }
    return result;
}
bool OSGBDatabasePager::requestValid(const vsg::PagedLOD& page) const
{
    std::lock_guard lock(_impl->mutex);
    return _impl->permitted(page);
}
void OSGBDatabasePager::releaseUploadedData(uint32_t deviceID)
{
    std::lock_guard lock(_impl->mutex);
    uint64_t total = 0;
    for (auto& [page, resident] : _impl->resident)
    {
        if (!resident.token || !resident.resources.upload) continue;
        auto& upload = *resident.resources.upload;
        const auto before = upload.bytes();
        upload.release(deviceID);
        const auto after = upload.bytes();
        _impl->memory.cpuBytesReleased += before - after;
        total += after;
    }
    _impl->memory.residentCpuBytes = total;
}
void OSGBDatabasePager::configure(const OSGBPagingLimits& limits)
{
    if (!limits.rootInFlight || !limits.detailInFlight || !limits.maxQueuedPerClass || !limits.maxReadyPages || !limits.maxMergesPerFrame ||
        !std::isfinite(limits.mergeTimeMs) || limits.mergeTimeMs < 0.0 ||
        !std::isfinite(limits.recentResidentSeconds) || limits.recentResidentSeconds < 0.0)
        throw std::invalid_argument("Invalid OSGB paging limits");
    std::lock_guard lock(_impl->mutex);
    _impl->limits = limits;
    _impl->changed.notify_all();
}
void OSGBDatabasePager::accountStage(const vsg::PagedLOD& page, int previous, int next)
{
    if (auto* token = pagingToken(page))
    {
        std::lock_guard lock(token->_metricsMutex);
        auto counter = [&](int stage) -> uint64_t* {
            switch (stage)
            {
                case vsg::PagedLOD::ReadRequest: return &token->_metrics.queued;
                case vsg::PagedLOD::Reading: return &token->_metrics.reading;
                case vsg::PagedLOD::Compiling: return &token->_metrics.compiling;
                case vsg::PagedLOD::MergeRequest: return &token->_metrics.ready;
                default: return nullptr;
            }
        };
        if (auto* value = counter(previous)) --*value;
        if (auto* value = counter(next)) ++*value;
    }
}
void OSGBDatabasePager::accountTime(const vsg::PagedLOD& page, Phase phase, double elapsedMs)
{
    auto add = [&](OSGBPagingTimings& timings) {
        switch (phase)
        {
            case Phase::Queue: timings.queueWaitMs += elapsedMs; break;
            case Phase::Read: timings.readMs += elapsedMs; break;
            case Phase::Compile: timings.compileMs += elapsedMs; break;
            case Phase::Merge: timings.mergeMs += elapsedMs; break;
        }
    };
    { std::lock_guard lock(_impl->mutex); add(_impl->totals); }
    if (auto* token = pagingToken(page))
    { std::lock_guard lock(token->_metricsMutex); add(token->_metrics); }
}
void OSGBDatabasePager::accountResident(OSGBPagingToken* token, uint64_t bytes, uint64_t draws, bool root, bool adding)
{
    if (!token) return;
    std::lock_guard lock(token->_metricsMutex);
    if (adding)
    {
        ++token->_metrics.residentPages;
        token->_metrics.residentRoots += root ? 1 : 0;
        token->_metrics.residentBytes += bytes;
        token->_metrics.residentDraws += draws;
        ++token->_metrics.merged;
    }
    else
    {
        --token->_metrics.residentPages;
        token->_metrics.residentRoots -= root ? 1 : 0;
        token->_metrics.residentBytes -= bytes;
        token->_metrics.residentDraws -= draws;
    }
}
OSGBPagingMetrics OSGBDatabasePager::metrics() const
{
    std::lock_guard lock(_impl->mutex);
    auto result = _impl->totals;
    result.queuedRoots = _impl->roots.size();
    result.queuedDetails = _impl->details.size();
    result.activeRoots = _impl->activeRoots;
    result.activeDetails = _impl->activeDetails;
    result.readyPages = _impl->ready.size();
    result.readyCpuBytes = _impl->readyBytes;
    result.compilingCpuBytes = _impl->reservedBytes;
    return result;
}
void OSGBDatabasePager::start(uint32_t count)
{
    std::lock_guard lock(_impl->mutex);
    if (_impl->started || !status->active()) return;
    if (!compileManager || !count) throw std::invalid_argument("OSGB pager requires a compiler and read workers");
    _impl->started = true;
    for (uint32_t i = 0; i < count; ++i) threads.emplace_back([this] { run(); });
    threads.emplace_back([this] { while (status->active()) deleteQueue->wait_then_clear(); });
}
void OSGBDatabasePager::shutdown()
{
    status->set(false);
    _impl->changed.notify_all();
    for (auto& thread : threads) if (thread.joinable()) thread.join();
    threads.clear();
    // 尚未挂接的结果不再可见；宿主仍须按 Viewer 退场顺序保持 Device 有效。
    std::deque<Impl::Request> roots, details;
    std::deque<Impl::Ready> ready;
    {
        std::lock_guard lock(_impl->mutex);
        roots.swap(_impl->roots); details.swap(_impl->details); ready.swap(_impl->ready);
        _impl->readyBytes = 0;
        for (const auto& [page, resident] : _impl->resident)
            accountResident(resident.token, resident.resources.bytes, resident.resources.draws, resident.root, false);
        _impl->resident.clear();
        _impl->totals.residentEstimatedBytes = 0;
    }
    for (auto& item : roots) discard(item.page, false);
    for (auto& item : details) discard(item.page, false);
    for (auto& item : ready) discard(item.page, false);
}
void OSGBDatabasePager::request(vsg::ref_ptr<vsg::PagedLOD> page)
{
    if (!page) return;
    // 队列和终态使用同一把锁；计数不参与门控，Record 每帧可安全重试。
    std::lock_guard lock(_impl->mutex);
    auto expected = vsg::PagedLOD::NoRequest;
    if (!page->requestStatus.compare_exchange_strong(expected, vsg::PagedLOD::ReadRequest)) return;
    auto& queue = dynamic_cast<OSGBRoot*>(page.get()) ? _impl->roots : _impl->details;
    if (!status->active() || !_impl->permitted(*page) || _impl->cooling(*page, frameCount.load()) ||
        queue.size() >= _impl->limits.maxQueuedPerClass)
    {
        page->requestCount.store(0);
        page->requestStatus.store(vsg::PagedLOD::NoRequest);
        ++_impl->totals.rejected;
        if (auto* token = pagingToken(*page))
        { std::lock_guard tokenLock(token->_metricsMutex); ++token->_metrics.rejected; }
        return;
    }
    page->requestCount.store(1);
    ++numActiveRequests;
    accountStage(*page, vsg::PagedLOD::NoRequest, vsg::PagedLOD::ReadRequest);
    queue.push_back({page, Clock::now()});
    _impl->changed.notify_one();
}
void OSGBDatabasePager::discard(vsg::ref_ptr<vsg::PagedLOD> page, bool failure)
{
    int previous = vsg::PagedLOD::NoRequest;
    {
        std::lock_guard lock(_impl->mutex);
        previous = page->requestStatus.load();
        if (previous == vsg::PagedLOD::NoRequest || previous == vsg::PagedLOD::DeleteRequest) return;
        page->requestStatus.store(vsg::PagedLOD::DeleteRequest);
    }
    vsg::ref_ptr<vsg::Node> unused;
    {
        std::lock_guard lock(pendingPagedLODMutex);
        unused = std::move(page->pending);
    }
    if (unused)
    {
        auto resources = footprint(*unused);
        {
            std::lock_guard lock(_impl->mutex);
            _impl->retired.push_back({NodeLifetime(unused), resources.bytes, std::move(resources.observed)});
            _impl->memory.retiringBytes += resources.bytes;
        }
        deleteQueue->add(vsg::ref_ptr<vsg::Object>(unused));
    }
    if (page->options && page->options->sharedObjects)
        deleteQueue->prune(vsg::ref_ptr<vsg::SharedObjects>(page->options->sharedObjects));
    {
        std::lock_guard lock(_impl->mutex);
        if (failure) page->frameNextLoadAttempt.store(frameCount.load() + delayBeforeNextLoadAttempt);
        page->requestCount.store(0);
        // 完成指标与可重入终态同时发布；清理 pending 期间仍算在途请求。
        accountStage(*page, previous, vsg::PagedLOD::NoRequest);
        page->requestStatus.store(vsg::PagedLOD::NoRequest);
        --numActiveRequests;
        if (failure) ++_impl->totals.failed; else ++_impl->totals.cancelled;
        if (auto* token = pagingToken(*page))
        {
            std::lock_guard tokenLock(token->_metricsMutex);
            if (failure) ++token->_metrics.failed; else ++token->_metrics.cancelled;
        }
        _impl->changed.notify_all();
    }
}
void OSGBDatabasePager::run()
{
    while (status->active())
    {
        vsg::ref_ptr<vsg::PagedLOD> page;
        bool root = false;
        uint64_t readReservation = 0, readClaim = 0;
        Clock::time_point queuedAt;
        double queueMs = 0.0;
        {
            std::unique_lock lock(_impl->mutex);
            // 队列分为覆盖/细化两类并轮转；每类内部按有限贡献和等待年龄排序。
            auto select = [&](std::deque<Impl::Request>& queue, bool rootQueue) {
                auto best = queue.end();
                double score = -1.0;
                const auto now = Clock::now();
                for (auto it = queue.begin(); it != queue.end(); ++it)
                {
                    if (!_impl->permitted(*it->page) || _impl->cooling(*it->page, frameCount.load()) ||
                        it->page->frameNextLoadAttempt.load() > frameCount.load()) continue;
                    if (!rootQueue && pagingToken(*it->page) && _impl->coveragePending(frameCount.load()))
                    { it->budgetBlocked = true; ++_impl->memory.budgetDeferrals; continue; }
                    if (pagingToken(*it->page))
                    {
                        const auto& limits = _impl->memoryLimits;
                        const auto held = _impl->heldBytes();
                        const auto cap = _impl->admissionLimit(rootQueue, it->requiredBytes);
                        const auto available = cap - std::min(cap, held);
                        // 未知尺寸允许使用剩余空间作一次受读预算约束的探测；不让保守估值饿死小页。
                        const auto claim = it->requiredBytes ? it->requiredBytes : std::min(limits.maxPageBytes, available);
                        const auto readBudget = std::max(limits.readReservationBytes, it->requiredBytes);
                        it->oversized = it->requiredBytes > cap || readBudget > limits.readWorkingBytes;
                        it->budgetBlocked = it->oversized || !claim ||
                            _impl->memory.readReservedBytes > limits.readWorkingBytes - std::min(readBudget, limits.readWorkingBytes) ||
                            claim > available;
                        if (it->budgetBlocked)
                        {
                            _impl->memory.limited = true;
                            ++_impl->memory.budgetDeferrals;
                            continue;
                        }
                    }
                    const double age = std::chrono::duration<double>(now - it->queued).count();
                    const double value = std::log1p(finitePriority(it->page->priority.load())) + age;
                    if (value > score) { best = it; score = value; }
                }
                return best;
            };
            auto r = select(_impl->roots, true), d = select(_impl->details, false);
            const bool haveRoot = r != _impl->roots.end() && _impl->activeRoots < _impl->limits.rootInFlight;
            const bool haveDetail = d != _impl->details.end() && _impl->activeDetails < _impl->limits.detailInFlight;
            if (!haveRoot && !haveDetail)
            {
                _impl->changed.wait_for(lock, std::chrono::milliseconds(10));
                continue;
            }
            root = haveRoot && (!haveDetail || pagingToken(*r->page) || pagingToken(*d->page) || _impl->preferRoot);
            if (haveRoot && haveDetail && !pagingToken(*r->page) && !pagingToken(*d->page)) _impl->preferRoot = !root;
            auto& queue = root ? _impl->roots : _impl->details;
            auto chosen = root ? r : d;
            page = chosen->page;
            queuedAt = chosen->queued;
            const auto knownBytes = chosen->requiredBytes;
            queueMs = std::chrono::duration<double, std::milli>(Clock::now() - chosen->queued).count();
            queue.erase(chosen);
            if (pagingToken(*page))
            {
                readReservation = std::max(_impl->memoryLimits.readReservationBytes, knownBytes);
                const auto cap = _impl->admissionLimit(root, knownBytes);
                readClaim = knownBytes ? knownBytes : std::min(_impl->memoryLimits.maxPageBytes, cap - std::min(cap, _impl->heldBytes()));
                _impl->memory.readReservedBytes += readReservation;
                _impl->readClaims += readClaim;
            }
            if (root) ++_impl->activeRoots; else ++_impl->activeDetails;
        }
        accountTime(*page, Phase::Queue, queueMs);
        auto releaseWorker = [&] {
            std::lock_guard lock(_impl->mutex);
            if (root) --_impl->activeRoots; else --_impl->activeDetails;
            _impl->memory.readReservedBytes -= readReservation;
            _impl->readClaims -= readClaim;
            _impl->changed.notify_all();
        };
        if (!requestValid(*page) || !recent(*page, frameCount.load()) || !status->active())
        {
            discard(page, false); releaseWorker(); continue;
        }
        page->requestStatus.store(vsg::PagedLOD::Reading);
        accountStage(*page, vsg::PagedLOD::ReadRequest, vsg::PagedLOD::Reading);
        ++page->loadAttempts;
        uint64_t reservation = 0;
        bool reserved = false, compiling = false;
        auto releaseReservation = [&] {
            std::lock_guard lock(_impl->mutex);
            _impl->reservedBytes -= reservation;
            if (compiling) _impl->compilingBytes -= reservation;
            compiling = false;
            if (reserved) { --_impl->reservedPages; _impl->scheduledBytes -= reservation; }
            reserved = false;
            reservation = 0;
            _impl->changed.notify_all();
        };
        auto measured = [&](Phase phase, auto operation) {
            const auto begin = Clock::now();
            try
            {
                auto value = operation();
                accountTime(*page, phase, std::chrono::duration<double, std::milli>(Clock::now() - begin).count());
                return value;
            }
            catch (...)
            {
                accountTime(*page, phase, std::chrono::duration<double, std::milli>(Clock::now() - begin).count());
                throw;
            }
        };
        try
        {
            auto node = measured(Phase::Read, [&] { return vsg::read_cast<vsg::Node>(page->filename, page->options); });
            if (!node)
            {
                discard(page, requestValid(*page)); releaseWorker(); continue;
            }
            if (!requestValid(*page) || !recent(*page, frameCount.load()) || !status->active())
            {
                discard(page, false); releaseWorker(); continue;
            }
            const auto resources = footprint(*node);
            const auto bytes = resources.bytes;
            // 解码后以实际载荷替换读取估值；预算等待只保留路径/令牌，不缓存解码对象。
            // 已知尺寸使下一次选择在可容纳前不重复读取，预算更新或退休会自动唤醒。
            bool defer = false;
            {
                std::unique_lock lock(_impl->mutex);
                _impl->readClaims -= readClaim; readClaim = 0;
                _impl->memory.readReservedBytes -= readReservation; readReservation = 0;
                if (pagingToken(*page))
                {
                    const auto cap = _impl->admissionLimit(root, bytes);
                    const bool oversized = bytes > cap || bytes > _impl->memoryLimits.readWorkingBytes;
                    defer = oversized || _impl->heldBytes() > cap - std::min(bytes, cap);
                    if (defer)
                    {
                        _impl->memory.limited = true;
                        ++_impl->memory.budgetDeferrals;
                        if (oversized) ++_impl->memory.oversizedRejected;
                        accountStage(*page, vsg::PagedLOD::Reading, vsg::PagedLOD::ReadRequest);
                        page->requestStatus.store(vsg::PagedLOD::ReadRequest);
                        (root ? _impl->roots : _impl->details).push_back({page, queuedAt, bytes, true, oversized});
                    }
                }
                if (!defer)
                {
                    _impl->reservedBytes += bytes;
                    reservation = bytes;
                    // 只允许一个超过 Ready 软上限的独立页进入空队列，不会永久饿死。
                    while (status->active() && _impl->permitted(*page) && recent(*page, frameCount.load()) &&
                        (_impl->ready.size() + _impl->reservedPages >= _impl->limits.maxReadyPages ||
                            (_impl->limits.readyCpuBytes && (_impl->readyBytes + _impl->scheduledBytes) &&
                                (bytes > _impl->limits.readyCpuBytes ||
                                    _impl->readyBytes + _impl->scheduledBytes > _impl->limits.readyCpuBytes - bytes))))
                        _impl->changed.wait_for(lock, std::chrono::milliseconds(10));
                    ++_impl->reservedPages;
                    _impl->scheduledBytes += bytes;
                    reserved = true;
                }
            }
            if (defer) { releaseWorker(); continue; }
            if (!requestValid(*page) || !recent(*page, frameCount.load()) || !status->active())
            {
                releaseReservation(); discard(page, false); releaseWorker(); continue;
            }
            page->requestStatus.store(vsg::PagedLOD::Compiling);
            accountStage(*page, vsg::PagedLOD::Reading, vsg::PagedLOD::Compiling);
            { std::lock_guard lock(_impl->mutex); _impl->compilingBytes += bytes; compiling = true; }
            auto result = measured(Phase::Compile, [&] { return compileManager->compile(node); });
            {
                std::lock_guard lock(pendingPagedLODMutex);
                page->pending = node;
            }
            if (!result || !requestValid(*page) || !recent(*page, frameCount.load()) || !status->active())
            {
                releaseReservation(); discard(page, !result && requestValid(*page)); releaseWorker(); continue;
            }
            page->requestStatus.store(vsg::PagedLOD::MergeRequest);
            accountStage(*page, vsg::PagedLOD::Compiling, vsg::PagedLOD::MergeRequest);
            {
                std::lock_guard lock(_impl->mutex);
                _impl->ready.push_back({page, result, resources});
                _impl->reservedBytes -= reservation;
                _impl->compilingBytes -= reservation;
                compiling = false;
                --_impl->reservedPages;
                _impl->scheduledBytes -= reservation;
                reserved = false;
                reservation = 0;
                _impl->readyBytes += bytes;
                if (_impl->limits.readyCpuBytes && bytes > _impl->limits.readyCpuBytes) ++_impl->totals.oversizePages;
            }
        }
        catch (...) { releaseReservation(); discard(page, requestValid(*page)); }
        releaseWorker();
    }
}

void OSGBDatabasePager::updateSceneGraph(vsg::ref_ptr<vsg::FrameStamp> stamp, vsg::CompileResult& result)
{
    if (!stamp) stamp = vsg::FrameStamp::create();
    frameCount.store(stamp->frameCount);
    const auto begin = Clock::now();
    OSGBPagingLimits limits;
    auto purgeRequests = [&] {
        std::deque<vsg::ref_ptr<vsg::PagedLOD>> cancelled;
        std::deque<Impl::Ready> cancelledReady;
        {
            std::lock_guard lock(_impl->mutex);
            for (auto* queue : {&_impl->roots, &_impl->details})
                for (auto it = queue->begin(); it != queue->end();)
                    if (!_impl->permitted(*it->page) || !recent(*it->page, stamp->frameCount))
                    { cancelled.push_back(it->page); it = queue->erase(it); }
                    else ++it;
            for (auto it = _impl->ready.begin(); it != _impl->ready.end();)
                if (!_impl->permitted(*it->page) || !recent(*it->page, stamp->frameCount))
                {
                    _impl->readyBytes -= it->resources.bytes;
                    cancelledReady.push_back(std::move(*it));
                    it = _impl->ready.erase(it);
                }
                else ++it;
        }
        for (auto& page : cancelled) discard(page, false);
        for (auto& item : cancelledReady) discard(item.page, false);
    };
    { std::lock_guard lock(_impl->mutex); limits = _impl->limits; }
    purgeRequests();
    vsg::DatabasePager::updateSceneGraph(stamp, result);
    // 原生 Update 先维护活动容器与回收；每个独立页再按实际挂接耗时逐项提交。
    uint32_t merged = 0;
    while (merged < limits.maxMergesPerFrame)
    {
        if (merged && limits.mergeTimeMs > 0.0 &&
            std::chrono::duration<double, std::milli>(Clock::now() - begin).count() >= limits.mergeTimeMs) break;
        Impl::Ready ready;
        {
            std::lock_guard lock(_impl->mutex);
            if (_impl->ready.empty()) break;
            ready = std::move(_impl->ready.front());
            _impl->ready.pop_front();
            _impl->readyBytes -= ready.resources.bytes;
            _impl->reservedBytes += ready.resources.bytes;
            _impl->changed.notify_all();
        }
        if (!requestValid(*ready.page) || !recent(*ready.page, stamp->frameCount))
        {
            discard(ready.page, false);
            std::lock_guard lock(_impl->mutex);
            _impl->reservedBytes -= ready.resources.bytes;
            continue;
        }
        const auto mergeBegin = Clock::now();
        if (auto* token = pagingToken(*ready.page); token && ready.page->pending)
        {
            adjustOSGBLOD(*ready.page->pending, token->viewportHeight.load(), token->projectionY.load());
        }
        if (auto* root = dynamic_cast<OSGBRoot*>(ready.page.get()); root && !root->boundTrusted)
        {
            // Ready 页的后台写入已结束；只在 Update 发布真实三维范围。
            vsg::ComputeBounds bounds;
            if (root->pending) root->pending->accept(bounds);
            if (bounds.bounds.valid())
            {
                const auto center = (bounds.bounds.min + bounds.bounds.max) * 0.5;
                const auto radius = vsg::length(bounds.bounds.max - bounds.bounds.min) * 0.5;
                if (std::isfinite(center.x) && std::isfinite(center.y) && std::isfinite(center.z) && std::isfinite(radius))
                {
                    root->bound = vsg::dsphere(center, std::max(radius, 1e-6));
                    root->boundTrusted = true;
                }
            }
        }
        // 每次只提交一页，原生 Update 负责全部容器链表和 CompileResult 传播。
        culledPagedLODs->newHighresRequired.push_back(ready.page.get());
        _toMergeQueue->add(ready.page, ready.result);
        vsg::DatabasePager::updateSceneGraph(stamp, result);
        accountStage(*ready.page, vsg::PagedLOD::MergeRequest, vsg::PagedLOD::NoRequest);
        accountTime(*ready.page, Phase::Merge, std::chrono::duration<double, std::milli>(Clock::now() - mergeBegin).count());
        {
            std::lock_guard lock(_impl->mutex);
            auto* token = pagingToken(*ready.page);
            const bool isRoot = dynamic_cast<OSGBRoot*>(ready.page.get()) != nullptr;
            _impl->resident[ready.page.get()] = {ready.resources, vsg::ref_ptr<OSGBPagingToken>(token), isRoot, Clock::now(),
                NodeLifetime(ready.page->children[0].node), NodeLifetime(ready.page)};
            _impl->reservedBytes -= ready.resources.bytes;
            _impl->totals.residentEstimatedBytes += ready.resources.bytes;
            accountResident(token, ready.resources.bytes, ready.resources.draws, isRoot, true);
            ++_impl->totals.merged;
        }
        ++merged;
    }
    // 全容器包含仅有粗模、读取中和被预算阻塞的页，不能用 fine 是否存在筛选所有权。
    std::map<vsg::PagedLOD*, vsg::ref_ptr<vsg::PagedLOD>> container;
    for (const auto& entry : pagedLODContainer->elements)
        if (entry.plod) container.emplace(entry.plod.get(), entry.plod);
    const auto now = Clock::now();
    std::set<vsg::PagedLOD*> processed;
    // 只通过容器已有强引用访问页；历史关系使用 Auxiliary 检查身份，禁止复活零引用对象。
    auto livePage = [&](const PageWatch& watch) -> vsg::ref_ptr<vsg::PagedLOD> {
        auto found = container.find(watch.page);
        if (found == container.end() || !watch.lifetime.alive()) return {};
        return NodeLifetime(found->second).auxiliary == watch.lifetime.auxiliary ? found->second : vsg::ref_ptr<vsg::PagedLOD>{};
    };
    std::function<void(PageWatch, bool)> retireTree;
    retireTree = [&](PageWatch watch, bool abandon) {
        if (!watch.page || !processed.insert(watch.page).second) return;
        Impl::Resident info;
        bool tracked = false;
        {
            std::lock_guard lock(_impl->mutex);
            auto found = _impl->resident.find(watch.page);
            if (found != _impl->resident.end() && found->second.wrapper.auxiliary == watch.lifetime.auxiliary)
            { info = found->second; tracked = true; }
            if (abandon && watch.lifetime.alive()) _impl->abandoned[watch.page] = {watch.lifetime, 0};
        }
        // 父载荷退出时，其所有动态分页后代都失去场景所有权，包括尚无 fine 的容器槽。
        for (const auto& child : info.resources.nestedPages) retireTree(child, true);
        auto page = livePage(watch);
        vsg::ref_ptr<vsg::Node> payload;
        if (page && pagingToken(*page))
        {
            {
                std::lock_guard lock(_impl->mutex);
                auto expected = vsg::PagedLOD::NoRequest;
                if (page->requestStatus.compare_exchange_strong(expected, vsg::PagedLOD::DeleteRequest))
                {
                    std::lock_guard pendingLock(pendingPagedLODMutex);
                    payload = page->pending ? std::move(page->pending) : std::move(page->children[0].node);
                    page->children[0].node = {};
                    page->requestCount.store(0);
                    page->requestStatus.store(vsg::PagedLOD::NoRequest);
                }
            }
            detachPage(*pagedLODContainer, *page);
            deleteQueue->add(vsg::ref_ptr<vsg::Object>(page));
            if (page->options && page->options->sharedObjects)
                deleteQueue->prune(vsg::ref_ptr<vsg::SharedObjects>(page->options->sharedObjects));
        }
        if (payload) deleteQueue->add(vsg::ref_ptr<vsg::Object>(payload));
        if (tracked)
        {
            std::lock_guard lock(_impl->mutex);
            // GPU/CPU 载荷观察独立于顶层 Group 的析构，退场不保留上传对象强引用。
            _impl->retired.push_back({info.node, info.resources.bytes, info.resources.observed});
            _impl->memory.retiringBytes += info.resources.bytes;
            if (_impl->resident.erase(watch.page))
            {
                _impl->totals.residentEstimatedBytes -= info.resources.bytes;
                accountResident(info.token, info.resources.bytes, info.resources.draws, info.root, false);
            }
            if (info.token) ++_impl->ownership.subtreeRetirements;
        }
    };
    // 原生按节点数淘汰也可能拆掉父载荷；通过合并时保存的弱关系收走全部孤儿后代。
    std::vector<PageWatch> lost;
    {
        std::lock_guard lock(_impl->mutex);
        for (const auto& [raw, info] : _impl->resident)
        {
            const auto found = container.find(raw);
            if (found == container.end() || !found->second->children[0].node ||
                NodeLifetime(found->second->children[0].node).auxiliary != info.node.auxiliary)
                lost.push_back({raw, info.wrapper});
        }
    }
    for (const auto& watch : lost) retireTree(watch, false);
    // 关闭图层无条件清全容器；普通失活空槽只摘容器引用，仍在父粗模中的节点可以再次请求。
    for (const auto& [raw, page] : container)
    {
        auto* token = pagingToken(*page);
        if (!token || processed.count(raw)) continue;
        if (token->closed()) retireTree({raw, NodeLifetime(page)}, true);
        else if (!page->children[0].node && !page->highResActive(stamp->frameCount))
        {
            detachPage(*pagedLODContainer, *page);
            deleteQueue->add(vsg::ref_ptr<vsg::Object>(page));
        }
    }
    std::vector<vsg::ref_ptr<vsg::PagedLOD>> oldest;
    for (const auto& [raw, page] : container)
        if (!processed.count(raw) && pagingToken(*page) && page->children[0].node) oldest.push_back(page);
    std::stable_sort(oldest.begin(), oldest.end(), [](const auto& a, const auto& b) {
        return a->frameHighResLastUsed.load() < b->frameHighResLastUsed.load();
    });
    uint64_t inactivePages = 0;
    for (const auto& page : oldest) if (!page->highResActive(stamp->frameCount)) ++inactivePages;
    for (const auto& page : oldest)
    {
        if (processed.count(page)) continue;
        bool retire = false;
        {
            std::lock_guard lock(_impl->mutex);
            auto found = _impl->resident.find(page);
            if (found == _impl->resident.end()) continue;
            auto& info = found->second;
            if (page->highResActive(stamp->frameCount)) { info.lastActive = now; continue; }
            const auto resident = _impl->totals.residentEstimatedBytes;
            const auto pending = _impl->readyBytes + _impl->reservedBytes + _impl->readClaims;
            const bool over = resident + pending > _impl->memoryLimits.residentBytes - _impl->memoryLimits.maxPageBytes ||
                (limits.residentBytesTarget && resident > limits.residentBytesTarget);
            const bool expired = limits.recentResidentSeconds > 0.0 &&
                std::chrono::duration<double>(now - info.lastActive).count() >= limits.recentResidentSeconds;
            retire = over || expired || (limits.maxInactivePages && inactivePages > limits.maxInactivePages);
        }
        if (retire)
        {
            retireTree({page, NodeLifetime(page)}, false);
            inactivePages = 0;
            for (const auto& candidate : oldest)
                if (!processed.count(candidate) && !candidate->highResActive(stamp->frameCount)) ++inactivePages;
        }
    }
    // 覆盖欠账优先于精化：只退让有真实粗模 fallback 的细节，根覆盖本身不被抢走。
    // 已退休载荷最终会释放；以将来可用字节决定退让量，避免同一延迟释放重复触发更多淘汰。
    auto coverageNeedsRoom = [&] {
        std::lock_guard lock(_impl->mutex);
        const auto cap = _impl->admissionLimit(true);
        const auto retained = _impl->totals.residentEstimatedBytes + _impl->readyBytes + _impl->reservedBytes + _impl->readClaims;
        for (const auto& request : _impl->roots)
        {
            if (!pagingToken(*request.page) || !_impl->permitted(*request.page) || !recent(*request.page, stamp->frameCount) || request.oversized) continue;
            const auto need = request.requiredBytes ? request.requiredBytes : std::min(_impl->memoryLimits.maxPageBytes, cap);
            if (need <= cap && retained > cap - need) return true;
        }
        return false;
    };
    std::stable_sort(oldest.begin(), oldest.end(), [](const auto& a, const auto& b) {
        return finitePriority(a->priority.load()) < finitePriority(b->priority.load());
    });
    for (const auto& page : oldest)
    {
        if (!coverageNeedsRoom()) break;
        if (processed.count(page) || dynamic_cast<OSGBRoot*>(page.get()) || !page->children[0].node ||
            !page->children[1].node || !drawableFallback(*page->children[1].node)) continue;
        {
            std::lock_guard lock(_impl->mutex);
            if (!_impl->resident.count(page)) continue;
            // 冷却只约束再次读取，不修改源 LOD；覆盖完成后细化仍可继续。
            _impl->cooldown[page] = {NodeLifetime(page), stamp->frameCount + 30};
            ++_impl->ownership.coveragePreemptions;
        }
        retireTree({page, NodeLifetime(page)}, false);
    }
    purgeRequests();
    {
        std::lock_guard lock(_impl->mutex);
        _impl->memory.retiringBytes = 0;
        for (auto it = _impl->retired.begin(); it != _impl->retired.end();)
            if (!it->alive()) it = _impl->retired.erase(it);
            else { _impl->memory.retiringBytes += it->bytes; ++it; }
        for (auto* map : {&_impl->abandoned, &_impl->cooldown})
            for (auto it = map->begin(); it != map->end();)
                if (!it->second.node.alive()) it = map->erase(it); else ++it;
        auto& ownership = _impl->ownership;
        ownership.containerPages = ownership.emptySlots = ownership.closedTokenPages = ownership.orphanPages = 0;
        for (const auto& entry : pagedLODContainer->elements)
        {
            if (!entry.plod || !pagingToken(*entry.plod)) continue;
            ++ownership.containerPages;
            if (!entry.plod->children[0].node) ++ownership.emptySlots;
            if (pagingToken(*entry.plod)->closed()) ++ownership.closedTokenPages;
            if (!_impl->permitted(*entry.plod) && !pagingToken(*entry.plod)->closed()) ++ownership.orphanPages;
        }
        _impl->memory.residentCpuBytes = 0;
        for (const auto& [page, info] : _impl->resident)
            if (info.resources.upload) _impl->memory.residentCpuBytes += info.resources.upload->bytes();
        _impl->memory.lodScale = 1.0;
        _impl->memory.limited = false;
        for (const auto* queue : {&_impl->roots, &_impl->details})
            for (const auto& request : *queue) _impl->memory.limited |= request.budgetBlocked;
        _impl->changed.notify_all();
    }

}

bool prepareOSGBPaging(vsg::Viewer& viewer, const OSGBPagingLimits& limits)
{
    if (viewer.recordAndSubmitTasks.empty()) return false;
    vsg::ref_ptr<OSGBDatabasePager> pager;
    for (const auto& task : viewer.recordAndSubmitTasks)
    {
        if (!task->databasePager) continue;
        auto existing = task->databasePager.cast<OSGBDatabasePager>();
        if (!existing || (pager && pager != existing) ||
            (existing->compileManager && existing->compileManager != viewer.compileManager) || !existing->status->active()) return false;
        pager = existing;
    }
    if (!pager) pager = OSGBDatabasePager::create();
    pager->configure(limits);
    for (auto& task : viewer.recordAndSubmitTasks) task->databasePager = pager;
    return true;
}
bool installOSGBPaging(vsg::Viewer& viewer, const OSGBPagingLimits& limits)
{
    if (!viewer.compileManager || !prepareOSGBPaging(viewer, limits)) return false;
    auto pager = viewer.recordAndSubmitTasks.front()->databasePager.cast<OSGBDatabasePager>();
    pager->compileManager = viewer.compileManager;
    if (!viewer.compileManager->resourceScavenger)
        viewer.compileManager->resourceScavenger = vsg::ResourceScavenger::create(pager);
    pager->start();
    return true;
}
}
