#pragma once
#include <geostudio/native/render/import/all/Export.h>
#include <vsg/nodes/Node.h>
#include <vsg/io/Options.h>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <map>
#include <string>
#include <vector>

namespace geostudio::native::io::detail
{
struct ArtifactContext
{
    std::filesystem::path sourceRoot, cacheRoot;
    // 调用者包含 CRS、origin、anchor、投影及 LOD 策略；生成器身份包含 SDK/转换器固定版本。
    std::string coordinateIdentity, generatorIdentity;
    bool colorTexturesSRGB = true;
    uint64_t maxCacheBytes = uint64_t{2} * 1024 * 1024 * 1024;
    uint64_t maxEntryBytes = uint64_t{128} * 1024 * 1024;
};
struct ArtifactPage
{
    std::filesystem::path sourceFile;
    std::vector<std::filesystem::path> externalImages;
    // Decode 返回的观测身份；提供时 store 必须匹配，防止旧转换结果绑定到新输入。
    std::string expectedSourceSha256;
    std::map<std::filesystem::path, std::string> expectedImageHashes;
};

// 缓存只保存单页转换结果，不包含外层地球 anchor，也不读取外部精模。
// 每个 Reader 独占实例；读取可并发，跨实例/进程写入使用非等待目录锁，竞争时直接回退。
class VSGXCHANGE_DECLSPEC OSGBArtifacts
{
public:
    enum class LookupState { Hit, Miss, Invalid, Disabled };
    enum class StoreState { Stored, AlreadyPresent, BudgetExceeded, Disabled, Failed };
    struct Lookup
    {
        LookupState state = LookupState::Miss;
        vsg::ref_ptr<vsg::Node> node;
        std::string diagnostic;
    };
    struct StoreResult
    {
        StoreState state = StoreState::Failed;
        std::string diagnostic;
    };
    struct Statistics
    {
        uint64_t hits = 0, misses = 0, invalid = 0, stored = 0, alreadyPresent = 0;
        uint64_t budgetExceeded = 0, failures = 0, bytesRead = 0, bytesWritten = 0;
        double loadMilliseconds = 0.0, storeMilliseconds = 0.0;
    };

    // 无效目录/配置使实例禁用并返回诊断；不创建输出，不影响源数据读取。
    explicit OSGBArtifacts(ArtifactContext context);
    ~OSGBArtifacts();
    // 返回独占的未编译图；分页路径重绑到当前 sourceRoot，Options 重绑到当前 Reader。
    // 任意缓存故障均返回 soft miss/Invalid，不抛为数据集加载错误。
    Lookup load(const std::filesystem::path& sourceFile, vsg::ref_ptr<const vsg::Options> pageOptions = {});
    // 仅借用私有、未 GPU 编译的页；暂时规范化分页路径，退出时总是恢复，不接管图。
    // 已加载的精模或不可无损往返的状态拒绝缓存；失败保留旧有效制品和受管 pending。
    StoreResult store(const ArtifactPage& source, vsg::Node& privatePage);
    // 返回线程安全的累计指标副本，不访问 GPU。
    Statistics statistics() const;
private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};
}
