#pragma once
#include <vsg/app/CompileManager.h>

namespace geostudio::native::detail
{
// 仅在宿主 Update 阶段使用；不持有 Viewer，不执行 GPU 操作。
// 固定版本 VSG 的 requiresViewerUpdate 不检查首次出现的 PagedLOD。
inline bool needsViewerUpdate(const vsg::CompileResult& result, const vsg::Viewer* viewer)
{
    return result && (result.containsPagedLOD || result.requiresViewerUpdate(viewer));
}
}
