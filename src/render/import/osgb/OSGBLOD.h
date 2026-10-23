#pragma once
#include <geostudio/native/render/import/all/Export.h>
#include <vsg/nodes/Node.h>
#include <vsg/maths/mat4.h>
#include <vsg/maths/sphere.h>

namespace geostudio::native::io::detail
{
// 纯 CPU 的源范围度量；输入为当前 Record 的 Vulkan 矩阵及实际像素视口。
// 不持有场景，不改状态；无效输入返回 NaN，由调用者保留粗模。
VSGXCHANGE_DECLSPEC double osgbRangeValue(const vsg::dsphere& sphere, bool pixels,
    const vsg::dmat4& projection, const vsg::dmat4& modelview, double width, double height);

// 仅在转换/反序列化后的独占子图上调用；替换带源范围元数据的页，保留共享子图与 Options。
// 专用 Record 选择不修改 VSG 通用页；输出仍以普通 PagedLOD 与元数据序列化。
VSGXCHANGE_DECLSPEC vsg::ref_ptr<vsg::Node> installOSGBLOD(vsg::ref_ptr<vsg::Node> node);
}
