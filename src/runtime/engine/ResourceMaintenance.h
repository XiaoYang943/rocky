#pragma once
#include <geostudio/native/core/base/Common.h>
#include <cstdint>

namespace vsg { class Viewer; }

namespace geostudio::native
{
    // Viewer 持有诊断状态，图层移除后继续有效；这是池统计，不是进程或显卡总内存。
    struct ResourceMaintenanceMetrics
    {
        std::uint64_t cpuReservedBytes = 0;
        std::uint64_t cpuAvailableBytes = 0;
        std::uint64_t cpuReleasedBytes = 0;
        std::uint64_t deviceReservedBytes = 0;
        std::uint64_t deviceAvailableBytes = 0;
        std::uint64_t compileStagingReservedBytes = 0;
        std::uint64_t compileStagingAvailableBytes = 0;
        std::uint64_t transferStagingBytes = 0;
        std::uint64_t transferStagingPeakBytes = 0;
        std::uint64_t deviceReleasedBytes = 0;
        std::uint64_t transferReleasedBytes = 0;
        std::uint64_t orphanedUploads = 0;
        std::uint64_t releasedDescriptorSets = 0;
        std::uint64_t maintenancePasses = 0;
        std::uint64_t pendingTransferFrames = 0;
        bool compileBusy = false;
        bool pending = false;
    };

    // 在宿主 Update 阶段请求提前维护；不等待工作线程或 GPU，不持有图层。
    GEOSTUDIO_NATIVE_API void requestResourceMaintenance(vsg::Viewer* viewer);
    // 在 Update 后、Record 前调用。每秒检查空闲池，仅释放无所有者或 fence 已完成的资源。
    // 正在编译或传输的资源延后重试；nullptr 无操作。Application 自动调用。
    GEOSTUDIO_NATIVE_API void maintainResources(vsg::Viewer* viewer);
    // 只读返回最近采样；同样在宿主 Update 线程调用，nullptr/未采样返回零值。
    GEOSTUDIO_NATIVE_API ResourceMaintenanceMetrics resourceMaintenanceMetrics(const vsg::Viewer* viewer);
}
