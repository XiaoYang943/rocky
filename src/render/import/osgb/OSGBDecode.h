#pragma once
#include <geostudio/native/render/import/all/Export.h>
#include <osg/Node>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <map>
#include <string>
#include <vector>

namespace geostudio::native::io::detail
{
struct DecodedSource
{
    osg::ref_ptr<osg::Node> node;
    std::vector<std::filesystem::path> externalImages;
    std::string sourceSha256;
    std::map<std::filesystem::path, std::string> externalImageHashes;
};

// 每个 Reader 持有一个解码入口；文件读取最多四个在途，固定 OSG 的兼容解码锁仍串行。
class VSGXCHANGE_DECLSPEC OSGBDecode
{
public:
    struct Settings
    {
        uint32_t maxInFlight = 4;
        uint64_t maxSourceBytes = 64 * 1024 * 1024;
    };
    struct Statistics
    {
        uint64_t requests = 0, completed = 0, failed = 0, cancelled = 0;
        uint64_t inFlight = 0, peakInFlight = 0;
        double admissionWaitMilliseconds = 0.0, ioMilliseconds = 0.0;
        double compatibilityLockWaitMilliseconds = 0.0, decodeMilliseconds = 0.0;
    };
    OSGBDecode();
    explicit OSGBDecode(Settings settings);
    ~OSGBDecode();
    // 可并发读取独立文件；返回不共享的节点及真实外部图片路径，禁止隐式读取外部模型。
    // 文件、插件或取消错误抛出可读异常；调用方必须持有本对象直到在途调用全部返回。
    DecodedSource read(const std::filesystem::path& path);
    // 幂等取消等待准入、等待兼容锁及文件分块读取；已进入第三方解码的结果返回前丢弃。
    void close();
    // 返回线程安全副本；统计的解码时长是 CPU 时间区间，不包含后续 Prepare/Convert/GPU。
    Statistics statistics() const;
private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};
}
