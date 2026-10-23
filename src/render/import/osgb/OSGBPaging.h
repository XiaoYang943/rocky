#pragma once
#include <geostudio/native/render/import/all/Export.h>
#include <vsg/io/DatabasePager.h>
#include <vsg/nodes/PagedLOD.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

namespace vsg { class Viewer; }
namespace geostudio::native::io
{
inline constexpr const char* osgbPagingTokenKey = "geostudio.osgb.paging-token";

// 阶段耗时为所有请求的累计 CPU 墙钟耗时（可重叠），不是首屏延迟。
struct OSGBPagingTimings
{
    double queueWaitMs = 0.0, readMs = 0.0, compileMs = 0.0, mergeMs = 0.0;
};
struct OSGBLayerPagingMetrics : OSGBPagingTimings
{
    uint64_t queued = 0, reading = 0, compiling = 0, ready = 0;
    uint64_t residentPages = 0, residentRoots = 0, residentDraws = 0, residentBytes = 0;
    uint64_t merged = 0, cancelled = 0, failed = 0, rejected = 0;
};

// 一个图层持有一个令牌，Options 向后代传递；关闭线程安全且不可重新打开。
class VSGXCHANGE_DECLSPEC OSGBPagingToken : public vsg::Inherit<vsg::Object, OSGBPagingToken>
{
public:
    void close() noexcept { _closed.store(true); }
    bool closed() const noexcept { return _closed.load(); }
    OSGBLayerPagingMetrics metrics() const;
    // 宿主 Update 发布最新视口；后台页在合并前重新应用，避免旧视口在途页晚到。
    std::atomic<double> viewportHeight{1080.0}, projectionY{1.7320508075688772};
private:
    friend class OSGBDatabasePager;
    std::atomic<bool> _closed{false};
    mutable std::mutex _metricsMutex;
    OSGBLayerPagingMetrics _metrics;
};

// 只重算带 OSGB split 标记的分页节点；宿主已挂接图限 Update 使用，独立页可在后台使用。
// 非法视口不改变节点；不访问纹理/几何 Data，不改变其他来源的分页阈值。
VSGXCHANGE_DECLSPEC void adjustOSGBLOD(vsg::Node& node, double height, double projectionY);

// 数据集运行时根页；保持零阈值的保守可见性，以有限投影贡献单独排序。
// Visitor/ConstVisitor 仍按 PagedLOD 派发；不作为可序列化的派生页类型。
class VSGXCHANGE_DECLSPEC OSGBRoot : public vsg::Inherit<vsg::PagedLOD, OSGBRoot>
{
public:
    OSGBRoot() = default;
    OSGBRoot(const OSGBRoot& rhs, const vsg::CopyOp& copy = {});
    // 索引只有平面范围时为 false；此时估计球只用于排序，不用于三维剔除。
    // 由宿主 Update 在根页挂接前补齐真实范围，Record 与 Update 不得并发。
    bool boundTrusted = true;
    vsg::ref_ptr<vsg::Object> clone(const vsg::CopyOp& copy = {}) const override;
    void accept(vsg::Visitor& visitor) override;
    void accept(vsg::ConstVisitor& visitor) const override;
    void accept(vsg::RecordTraversal& visitor) const override;
    void read(vsg::Input& input) override;
    void write(vsg::Output& output) const override;
    // 纯 CPU 分值；退化距离或非有限输入不能产生无穷大队列优先级。
    static double projectedPriority(double radius, double lodDistance) noexcept;
};

struct OSGBPagingLimits
{
    uint32_t rootInFlight = 2, detailInFlight = 4, maxQueuedPerClass = 256;
    uint32_t maxReadyPages = 64;
    uint64_t readyCpuBytes = 128ull * 1024 * 1024;
    uint64_t residentBytesTarget = 512ull * 1024 * 1024;
    uint32_t maxMergesPerFrame = 8;
    double mergeTimeMs = 2.0;
    // 近期缓存就是当前图的非活动驻留页；不克隆/重用有请求状态的运行时 Node。
    // 仅对带令牌的 OSGB 页生效；零表示禁用对应期限/数量限制。
    double recentResidentSeconds = 30.0;
    uint32_t maxInactivePages = 128;
};

struct OSGBPagingMetrics : OSGBPagingTimings
{
    uint64_t queuedRoots = 0, queuedDetails = 0, activeRoots = 0, activeDetails = 0;
    uint64_t readyPages = 0, readyCpuBytes = 0, compilingCpuBytes = 0, residentEstimatedBytes = 0;
    uint64_t merged = 0, cancelled = 0, failed = 0, rejected = 0, oversizePages = 0;
};

// 新增独立策略，保持旧 Limits/Metrics 的二进制布局。预算是资源载荷估算，不是进程 RSS。
struct OSGBMemoryLimits
{
    uint64_t residentBytes = 1024ull * 1024 * 1024;
    uint64_t readWorkingBytes = 512ull * 1024 * 1024;
    uint64_t readReservationBytes = 256ull * 1024 * 1024;
    // 未知页读取前的保守载荷估值；解码后由实测值替换，不是永久拒绝大页的阈值。
    uint64_t maxPageBytes = 128ull * 1024 * 1024;
};
struct OSGBMemoryMetrics
{
    uint64_t readReservedBytes = 0, retiringBytes = 0, residentCpuBytes = 0;
    uint64_t cpuBytesReleased = 0, budgetDeferrals = 0, oversizedRejected = 0;
    uint64_t residentLimit = 0, readWorkingLimit = 0;
    double lodScale = 1.0;
    bool limited = false;
};

// transitionBytes 限制父子共存余量；覆盖请求可使用该余量，资源总额仍受限。
// detailReserveBytes 保留历史字段名，现表示细化让出的覆盖余量；根覆盖优先使用。
// 实际值分别不超过 residentBytes 的 1/4 和 1/8；零可显式禁用。
struct OSGBResidencyLimits
{
    uint64_t transitionBytes = 256ull * 1024 * 1024;
    uint64_t detailReserveBytes = 128ull * 1024 * 1024;
};
// Pager 全局快照，在图层移除后仍可查询；均为载荷估计，不能相加当作进程私有内存。
// GPU 估计不含 Vulkan 对齐、驱动及空闲池；CPU 只统计尚被上传对象引用的 Data。
struct OSGBResidencyMetrics
{
    uint64_t residentPages = 0, residentGpuEstimatedBytes = 0, residentCpuBytes = 0;
    uint64_t decodedCpuBytes = 0, compilingCpuBytes = 0, readyCpuBytes = 0;
    uint64_t readReservedBytes = 0, readClaimBytes = 0, retiringPages = 0, retiringBytes = 0;
    uint64_t budgetBlockedPages = 0, oversizedWaitingPages = 0;
    uint64_t transitionLimit = 0, detailReserveBytes = 0;
};

// 全容器所有权快照；退休观察仅保留 Auxiliary，不延长几何、纹理或分页节点寿命。
struct OSGBPagingOwnershipMetrics
{
    uint64_t containerPages = 0, emptySlots = 0, closedTokenPages = 0, orphanPages = 0;
    uint64_t queuedCoverage = 0, coverageBlockedPages = 0;
    uint64_t coveragePreemptions = 0, subtreeRetirements = 0, retiredTrackedObjects = 0;
};

// 共用 Viewer 原有单 CompileManager；自有工作线程只处理独立页，宿主 Update 合并。
// 字节数按独立 Data 估算，不代表 Vulkan 分配；预算等待保留父页，允许配置和退休后恢复。
class VSGXCHANGE_DECLSPEC OSGBDatabasePager : public vsg::Inherit<vsg::DatabasePager, OSGBDatabasePager>
{
public:
    OSGBDatabasePager();
    ~OSGBDatabasePager() override;
    // 启动前/运行中均可配置；非法限制抛异常，配置和指标快照均线程安全。
    void configure(const OSGBPagingLimits& limits);
    OSGBPagingMetrics metrics() const;
    // 线程安全；预算必须为正且能容纳一个最大页与一次读取，否则抛异常。
    void configureMemory(const OSGBMemoryLimits& limits);
    OSGBMemoryMetrics memoryMetrics() const;
    // 配置与快照线程安全；配置更新会唤醒预算等待，不改变任何 LOD 选择条件。
    void configureResidency(const OSGBResidencyLimits& limits);
    OSGBResidencyMetrics residencyMetrics() const;
    // 线程安全；图层移除后仍可检查容器残留及真实载荷对象的退休状态。
    OSGBPagingOwnershipMetrics ownershipMetrics() const;
    // 仅宿主 Update、单 Device 图层调用；已完成 CPU→上传暂存复制的静态数据才释放。
    // 未编译、待复制及动态数据保留；不等待 GPU，不释放 GPU 缓冲和纹理。
    // 已释放的 CPU 几何不能再用于拾取、导出或第二个 Device 编译，需重新读取源页。
    void releaseUploadedData(uint32_t deviceID);
    void start(uint32_t numReadThreads = 4) override;
    void request(vsg::ref_ptr<vsg::PagedLOD> page) override;
    void updateSceneGraph(vsg::ref_ptr<vsg::FrameStamp> stamp, vsg::CompileResult& result) override;
    // 宿主退场时调用，等后台读取/编译退出；不可在本对象工作线程调用，不可重新启动。
    void shutdown();
private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
    void run();
    bool requestValid(const vsg::PagedLOD& page) const;
    void discard(vsg::ref_ptr<vsg::PagedLOD> page, bool failure);
    void accountStage(const vsg::PagedLOD& page, int previous, int next);
    enum class Phase { Queue, Read, Compile, Merge };
    void accountTime(const vsg::PagedLOD& page, Phase phase, double elapsedMs);
    void accountResident(OSGBPagingToken* token, uint64_t bytes, uint64_t draws, bool root, bool adding);
};

// 在首次动态分页前、宿主 Update 阶段调用；不替换其他已安装 Pager，不改变 CompileTraversal 数量。
// 成功共享给 Viewer 的全部任务；无 CompileManager/任务或已有不兼容 Pager 时返回 false。
VSGXCHANGE_DECLSPEC bool installOSGBPaging(vsg::Viewer& viewer, const OSGBPagingLimits& limits = {});
// 初次 Viewer::compile 前准备任务；编译器为空时仅挂载，交给 Viewer 赋编译器并启动。
VSGXCHANGE_DECLSPEC bool prepareOSGBPaging(vsg::Viewer& viewer, const OSGBPagingLimits& limits = {});
}
