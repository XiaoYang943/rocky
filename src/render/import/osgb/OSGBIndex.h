#pragma once
#include <geostudio/native/render/import/all/Export.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace geostudio::native::io::detail
{
struct FileIdentity
{
    uint64_t size = 0;
    int64_t modifiedTicks = 0;
    std::string sha256;
};
struct PlanarBounds
{
    std::array<double, 4> xy{};
};
struct RootRecord
{
    std::string relativePath;
    FileIdentity source;
    std::optional<PlanarBounds> footprint;
    std::optional<std::array<double, 4>> localEcefSphere;
};
struct DatasetIndex
{
    std::filesystem::path sourceRoot;
    std::string srs;
    std::array<double, 3> origin{};
    std::optional<std::array<double, 3>> ecefAnchor;
    std::string coordinatePolicy;
    std::string metadataContents;
    std::vector<RootRecord> roots;
};
struct IndexReadOptions
{
    std::filesystem::path sourceRootOverride;
    std::filesystem::path cacheDirectory;
    std::string coordinatePolicy = "ecef-v1";
};
// 只读目录/metadata.xml/SHP及可选范围缓存；不解码 OSGB，不写原件或缓存。
// 返回独立值对象；GDAL 句柄限于调用线程。定位冲突、路径逃逸及无有效根时抛异常。
VSGXCHANGE_DECLSPEC DatasetIndex readDatasetIndex(const std::filesystem::path& input, const IndexReadOptions& options = {});
// 在独立目录原子发布目录缓存；真实球须带调用者读取时的源 SHA-256，发布前再次核验。
// 不补造三维范围，不读 OSGB 几何；调用者负责真实球及坐标策略，失败保留旧有效索引。
VSGXCHANGE_DECLSPEC void publishDatasetIndex(const DatasetIndex& index, const std::filesystem::path& cacheDirectory);
}
