#include "../globe/catch.hpp"
#include "../../src/render/vsg/CompileResultUtils.h"
#include <vsg/all.h>

TEST_CASE("Empty paged roots require a viewer update after successful compilation", "[paging]")
{
    // 无窗口和 Device 的 CompileManager 只验证 CPU 编译结果及宿主更新决策。
    auto viewer = vsg::Viewer::create();
    auto compiler = vsg::CompileManager::create(*viewer, vsg::ref_ptr<vsg::ResourceHints>{});
    auto group = vsg::Group::create();
    group->addChild(vsg::PagedLOD::create());
    const auto result = compiler->compile(group);
    REQUIRE(result);
    REQUIRE(result.containsPagedLOD);
    CHECK_FALSE(result.requiresViewerUpdate(viewer));
    CHECK(geostudio::native::detail::needsViewerUpdate(result, viewer));
    CHECK_FALSE(geostudio::native::detail::needsViewerUpdate(compiler->compile(vsg::Group::create()), viewer));
    auto failed = result;
    failed.result = VK_ERROR_UNKNOWN;
    CHECK_FALSE(geostudio::native::detail::needsViewerUpdate(failed, viewer));
}
