#pragma once
#include <geostudio/native/render/import/all/Export.h>
#include <vsg/nodes/Node.h>
#include <cstdint>
#include <memory>

namespace geostudio::native::io::detail
{
// 仅借用调用者独占的未编译图；将 Image.data 暂时替换为标准独立 Array2D，退出总是恢复。
// 不读外部分页，不共享可变状态；内存预算/布局不支持时抛异常，已替换部分也恢复。
class VSGXCHANGE_DECLSPEC OSGBPlainImages
{
public:
    OSGBPlainImages(vsg::Node& node, uint64_t maximumBytes);
    ~OSGBPlainImages();
private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};
}
