#pragma once
#include <geostudio/native/render/import/all/Export.h>
#include <vsg/io/ReaderWriter.h>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
#include <future>
#include <functional>
#include <vsg/nodes/Node.h>

namespace geostudio::native::io
{
// 每个图层独占一个 Reader；后台只生成独立子图，宿主负责 Compile/Update。
class VSGXCHANGE_DECLSPEC OSGB : public vsg::Inherit<vsg::ReaderWriter, OSGB>
{
public:
    OSGB();
    ~OSGB() override;
    // 读取文件或带 metadata.xml 的数据集；失败返回空并保存诊断，不改写输入。
    vsg::ref_ptr<vsg::Object> read(const vsg::Path& filename, vsg::ref_ptr<const vsg::Options> options = {}) const override;
    // 后台执行相同 CPU 读取；future 必须保留到完成，关闭可取消后续阶段，GPU 编译仍由宿主执行。
    std::future<vsg::ref_ptr<vsg::Node>> readAsync(const vsg::Path& filename, vsg::ref_ptr<const vsg::Options> options = {});
    bool getFeatures(Features& features) const override;
    // 线程安全、幂等；关闭后拒绝新请求，进行中的结果在返回前丢弃。
    void close();
    // 返回最近一次失败的副本；可由宿主线程读取，不表示画面验收结果。
    std::string error() const;
    struct Status
    {
        uint64_t readRequests = 0, readCompleted = 0, readFailures = 0, readCancelled = 0, reading = 0;
        uint64_t pagedNodes = 0, attachedPages = 0, queuedPages = 0, readingPages = 0;
        uint64_t compilingPages = 0, mergePendingPages = 0, deferredPages = 0, residentDraws = 0;
        bool closed = false;
        std::string error;
        std::vector<std::string> deferredFiles;
    };
    // 返回诊断副本，不持有 root；传 root 时只能在宿主 Update 阶段调用，避免与 Merge 并发。
    // 只遍历已挂接子图，不读取后台 pending；驻留 Draw 不代表本帧可见或像素验收。
    Status status(const vsg::Node* root = nullptr, uint64_t frameCount = 0) const;
    struct Settings
    {
        // 倾斜摄影颜色纹理通常为 sRGB；数值纹理始终保留原语义。
        bool colorTexturesSRGB = true;
        std::string sourceRoot;
        // 独立制品目录；空表示不落盘，由 SDK 按源定位生成子目录。
        std::string cacheDirectory;
        uint32_t maxReadInFlight = 4;
        uint64_t maxCacheBytes = 2ull * 1024 * 1024 * 1024;
        uint64_t maxCacheEntryBytes = 128ull * 1024 * 1024;
    };
    // 首次读取前配置，读取开始后禁止更改资源语义。
    void configure(const Settings& settings);
    // 首次读取前显式启用模型磁盘缓存；默认仅使用 cacheDirectory 保存轻量范围索引。
    // 配置冻结后抛异常；不删除已有缓存，不改变 Settings 的 ABI 布局。
    void enablePageCache(bool enabled);
    struct Performance
    {
        double decodeMilliseconds = 0, prepareMilliseconds = 0, convertMilliseconds = 0;
        double resourceMilliseconds = 0, shaderMilliseconds = 0;
        uint64_t pipelinesCreated = 0, pipelineHits = 0, shadersCreated = 0, shaderHits = 0;
        uint64_t samplersCreated = 0, samplerHits = 0, colorImagesConverted = 0;
        double sourceIoMilliseconds = 0, decodeLockWaitMilliseconds = 0;
        uint64_t indexedRoots = 0, cachedBounds = 0;
        uint64_t cacheHits = 0, cacheMisses = 0, cacheWrites = 0;
        double cacheLoadMilliseconds = 0, cacheStoreMilliseconds = 0;
        std::string cacheDiagnostic;
    };
    // 累计 CPU 阶段耗时含相应锁等待；不代表 GPU 完成时间。
    Performance performance() const;
    // 仅宿主 Update 调用；projectionY 取当前投影矩阵 [1][1] 的绝对值，分页合并时同步最新值。
    void updateView(vsg::Node* attachedRoot, double viewportHeight, double projectionY);
    struct BuildSettings
    {
        std::string outputDirectory, sourceRoot;
        uint32_t rootsPerRegion = 16, textureMaxDimension = 256;
        double triangleRatio = 0.25, maximumSimplificationError = 1.0, regionScreenHeightRatio = 0.5;
        uint64_t maxWorkingBytes = 512ull * 1024 * 1024;
        uint64_t maxOutputBytes = 2ull * 1024 * 1024 * 1024;
        bool colorTexturesSRGB = true;
        std::function<bool()> cancelled;
        std::function<void(uint32_t completed, uint32_t total)> progress;
    };
    struct BuildResult
    {
        std::string manifestPath;
        uint64_t roots = 0, regions = 0, outputBytes = 0, inputTriangles = 0, outputTriangles = 0;
        bool reused = false;
    };
    // 离线 CPU 构建根级区域总览与完整根组，不读取精模后代，不写输入；失败/取消抛异常。
    // 输出清单最后发布，预算统计已知 Data/序列化载荷，不能视为进程 RSS 硬上限。
    static BuildResult buildDataset(const vsg::Path& input, const BuildSettings& settings);
    // 当前构建是否包含解析器；关闭可选功能时返回 false。
    static bool available();
private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};
}
EVSG_type_name(geostudio::native::io::OSGB);
