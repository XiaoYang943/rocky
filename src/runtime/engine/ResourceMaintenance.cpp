#include <geostudio/native/runtime/engine/ResourceMaintenance.h>
#include <vsg/app/Viewer.h>
#include <vsg/app/CompileManager.h>
#include <vsg/app/TransferTask.h>
#include <vsg/core/Allocator.h>
#include <chrono>
#include <set>

namespace geostudio::native
{
namespace
{
    constexpr const char* stateKey = "geostudio.resourceMaintenance";
    using Clock = std::chrono::steady_clock;

    // 只由 Viewer 的更新线程读写；状态不持有 Viewer，避免形成引用环。
    struct MaintenanceState : vsg::Inherit<vsg::Object, MaintenanceState>
    {
        ResourceMaintenanceMetrics metrics;
        Clock::time_point nextPass{};
        Clock::time_point settleUntil{};
    };

    MaintenanceState* state(vsg::Viewer* viewer)
    {
        if (auto* result = dynamic_cast<MaintenanceState*>(viewer->getObject(stateKey))) return result;
        auto result = MaintenanceState::create();
        viewer->setObject(stateKey, result);
        return result;
    }
}

void requestResourceMaintenance(vsg::Viewer* viewer)
{
    if (!viewer) return;
    auto* current = state(viewer);
    current->metrics.pending = true;
    current->nextPass = {};
    current->settleUntil = Clock::now() + std::chrono::seconds(3);
}

void maintainResources(vsg::Viewer* viewer)
{
    if (!viewer) return;
    auto* current = state(viewer);
    const auto now = Clock::now();
    if (now < current->nextPass) return;
    current->nextPass = now + std::chrono::seconds(1);
    auto& metrics = current->metrics;
    metrics.transferStagingBytes = 0;
    metrics.pendingTransferFrames = 0;
    std::set<vsg::ref_ptr<vsg::TransferTask>> transfers;
    for (const auto& task : viewer->recordAndSubmitTasks)
        if (task->transferTask) transfers.insert(task->transferTask);
    for (const auto& window : viewer->windows())
        if (auto device = window->getDevice())
            if (auto transfer = device->transferTask.ref_ptr()) transfers.insert(transfer);
    std::uint64_t stagingBefore = 0;
    for (const auto& transfer : transfers)
    {
        const auto usage = transfer->releaseUnusedResources();
        metrics.transferStagingBytes += usage.stagingBytes;
        stagingBefore += usage.stagingBytes + usage.releasedStagingBytes;
        metrics.transferReleasedBytes += usage.releasedStagingBytes;
        metrics.orphanedUploads += usage.orphanedEntries;
        metrics.pendingTransferFrames += usage.pendingFrames;
    }
    metrics.transferStagingPeakBytes = std::max(metrics.transferStagingPeakBytes, stagingBefore);
    metrics.compileBusy = false;
    if (viewer->compileManager)
    {
        const auto usage = viewer->compileManager->releaseUnusedResources();
        metrics.compileBusy = usage.busy;
        if (!usage.busy)
        {
            metrics.deviceReservedBytes = usage.deviceReservedBytes;
            metrics.deviceAvailableBytes = usage.deviceAvailableBytes;
            metrics.compileStagingReservedBytes = usage.stagingReservedBytes;
            metrics.compileStagingAvailableBytes = usage.stagingAvailableBytes;
            metrics.deviceReleasedBytes += usage.releasedBytes;
            metrics.releasedDescriptorSets += usage.releasedDescriptorSets;
        }
    }
    auto& allocator = *vsg::Allocator::instance();
    metrics.cpuReleasedBytes += allocator.deleteEmptyMemoryBlocks();
    metrics.cpuReservedBytes = allocator.totalReservedSize();
    metrics.cpuAvailableBytes = allocator.totalAvailableSize();
    ++metrics.maintenancePasses;
    metrics.pending = metrics.compileBusy || metrics.pendingTransferFrames != 0 || now < current->settleUntil;
}

ResourceMaintenanceMetrics resourceMaintenanceMetrics(const vsg::Viewer* viewer)
{
    if (viewer)
        if (const auto* current = dynamic_cast<const MaintenanceState*>(viewer->getObject(stateKey))) return current->metrics;
    return {};
}
}
