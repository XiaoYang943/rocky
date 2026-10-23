#pragma once
#include <geostudio/native/render/import/all/Export.h>
#include <vsg/nodes/Node.h>
#include <cstdint>
#include <memory>

namespace geostudio::native::io::detail
{
enum class OSGBColorSpace { SRGB, Linear };

// 每个 Reader 持有一份缓存；缓存不持有场景、分页器或纹理，析构不主动释放仍被场景引用的 GPU 对象。
class VSGXCHANGE_DECLSPEC OSGBResources
{
public:
    struct Statistics
    {
        uint64_t pipelinesCreated = 0, pipelineHits = 0;
        uint64_t shaderModulesCreated = 0, shaderHits = 0;
        uint64_t layoutsCreated = 0, layoutHits = 0;
        uint64_t samplersCreated = 0, samplerHits = 0, colorImagesConverted = 0;
        double shaderCompileMilliseconds = 0.0, resourceProcessMilliseconds = 0.0;
    };

    OSGBResources();
    ~OSGBResources();
    // 只处理调用者独占、尚未 GPU 编译的子图，可由多个读取线程并发调用；不读取外部分页。
    // 同一缓存交付的图必须使用宿主串行 GPU 编译入口；编译失败抛出异常，不发布失败的 Shader。
    void process(vsg::Node& node, OSGBColorSpace colorSpace = OSGBColorSpace::SRGB);
    // 返回线程安全的累计快照；不访问场景或 GPU。
    Statistics statistics() const;
private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};
}
