#pragma once
#include <geostudio/native/render/import/all/Export.h>
#include <osg/Node>
#include <osg/ref_ptr>
#include <cstdint>
#include <functional>

namespace geostudio::native::io::detail
{
struct OverviewSettings
{
    // 目标比例不是强制结果；边界点和拓扑约束可能阻止继续简化。
    double triangleRatio = 0.25;
    // 简化器的局部距离指标，不是地理误差或屏幕像素误差承诺。
    double maximumError = 1.0;
    uint32_t textureMaxDimension = 256;
    // 已解码数组与纹理的合计预算，另受几何和像素上限约束；不限制进程 RSS。
    uint64_t maxResidentBytes = 64ull * 1024 * 1024;
    uint64_t maxTrianglesPerGeometry = 250000;
    uint64_t maxTexturePixels = 16ull * 1024 * 1024;
    bool colorTexturesSRGB = true;
    std::function<bool()> cancelled;
};
struct OverviewStats
{
    uint64_t inputTriangles = 0, outputTriangles = 0;
    uint64_t inputVertices = 0, outputVertices = 0;
    uint64_t inputTextureBytes = 0, outputTextureBytes = 0;
    uint64_t texturesResized = 0, pagedLodsRemoved = 0;
    uint64_t estimatedPeakBytes = 0;
};
struct OverviewResult
{
    osg::ref_ptr<osg::Node> node;
    OverviewStats stats;
};
// 输入为完成坐标准备且尚未挂接的静态根，调用者保持只读；函数内部深拷贝后处理。
// 仅使用驻留内容，不读外部页、不创建 GPU；独占副本可在任意后台线程使用。
// 单位零按照片颜色处理，Alpha 线性预乘过滤后恢复非预乘，其他单位保持原样。
// 取消、待缩小纹理的未知格式或输入规模超限抛异常；估算峰值不是进程内存硬上限。
VSGXCHANGE_DECLSPEC OverviewResult reduceResident(const osg::Node& preparedRoot, const OverviewSettings& settings = {});
}
