#pragma once
#include "OSGBIndex.h"
#include <vsg/nodes/Node.h>
#include <vsg/io/Options.h>
#include <vsg/maths/sphere.h>
#include <functional>

namespace geostudio::native::io::detail
{
struct SourceDependency
{
    std::string relativePath;
    FileIdentity identity;
};
struct PreparedRoot
{
    // 独立未编译页；full 含完整粗模与原始子页链接，overview 为无分页的静态粗览。
    vsg::ref_ptr<vsg::Node> full, overview;
    vsg::dsphere bound;
    uint64_t workingBytes = 0, inputTriangles = 0, outputTriangles = 0;
    std::vector<SourceDependency> sourceDependencies;
};
struct DatasetBuildSettings
{
    std::filesystem::path outputDirectory;
    uint32_t rootsPerRegion = 16;
    uint64_t maxOutputBytes = 2ull * 1024 * 1024 * 1024;
    uint64_t maxWorkingBytes = 512ull * 1024 * 1024;
    double regionScreenHeightRatio = 0.5;
    std::string conversionIdentity;
    std::string overviewIdentity;
};
struct DatasetBuildResult
{
    std::filesystem::path manifestPath;
    uint64_t roots = 0, regions = 0, outputBytes = 0, inputTriangles = 0, outputTriangles = 0;
    bool reused = false;
};
struct DatasetRegion
{
    std::string id, entry, full, overview;
    FileIdentity fullIdentity, overviewIdentity, entryIdentity;
    vsg::dsphere bound;
    std::vector<SourceDependency> sources;
    std::vector<std::string> childReferences;
};
struct DatasetManifest
{
    std::filesystem::path manifestPath, sourceRoot;
    DatasetIndex metadata;
    FileIdentity metadataIdentity;
    std::string conversionIdentity;
    std::string overviewIdentity, sourceIndexIdentity;
    uint32_t rootsPerRegion = 16;
    double regionScreenHeightRatio = 0.5;
    vsg::dsphere bound;
    std::vector<DatasetRegion> regions;
    DatasetBuildResult statistics;
};
using PrepareDatasetRoot = std::function<PreparedRoot(const RootRecord&, uint64_t remainingWorkingBytes)>;
using DatasetCancelled = std::function<bool()>;

// 同步 CPU 构建；调用者提供坐标一致的索引/转换回调，回调不得调用 GPU。
// 全部根须具备 XY footprint 或真实 sphere 供 Morton 排序；失败/取消抛异常，旧入口保持有效。
// 预算约束制品字节和已知节点载荷，不声称限制第三方解码器内部瞬时堆分配。
VSGXCHANGE_DECLSPEC DatasetBuildResult buildDataset(const DatasetIndex& index, const DatasetBuildSettings& settings,
    const PrepareDatasetRoot& prepare, const DatasetCancelled& cancelled = {});
// 只读小入口与源 metadata；制品、root/texture SHA 在每个区域消费前验证，后代仅检查路径存在。
VSGXCHANGE_DECLSPEC DatasetManifest inspectManifest(const std::filesystem::path& path,
    const std::string& expectedConversionIdentity, const std::filesystem::path& sourceRootOverride = {});
// 独立 CPU 图；region high 仍指向 .osgbregion，由当前 SDK Reader 继续分页。
VSGXCHANGE_DECLSPEC vsg::ref_ptr<vsg::Node> loadOverviewGroup(const DatasetManifest& manifest,
    vsg::ref_ptr<const vsg::Options> options = {});
VSGXCHANGE_DECLSPEC vsg::ref_ptr<vsg::Node> readRegion(const std::filesystem::path& path,
    const std::string& expectedConversionIdentity, const std::filesystem::path& sourceRootOverride = {},
    vsg::ref_ptr<const vsg::Options> options = {});
}
