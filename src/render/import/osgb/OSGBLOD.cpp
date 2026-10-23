#include "OSGBLOD.h"
#include "OSGBPaging.h"
#include <vsg/all.h>
#include <vsg/vk/State.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

namespace geostudio::native::io::detail
{
double osgbRangeValue(const vsg::dsphere& sphere, bool pixels, const vsg::dmat4& projection,
    const vsg::dmat4& modelview, double width, double height)
{
    if (!pixels)
    {
        // OSG 使用局部空间中的视点距离；逆矩阵也保留非均匀模型缩放的语义。
        const auto eye = vsg::inverse(modelview) * vsg::dvec3(0.0, 0.0, 0.0);
        return vsg::length(sphere.center - eye);
    }
    if (!(width > 0.0 && height > 0.0 && sphere.r > 0.0)) return std::numeric_limits<double>::quiet_NaN();
    // 将 Vulkan 向下的 Y 恢复为 OSG 向上的 Y；深度映射不参与像素大小。
    // 与固定 OSG CullingSet 的屏幕投影导数相同，保留其 float 精度与非对称视锥项。
    const float px = float(projection[0][0] * width * 0.5);
    const float py = float(-projection[1][1] * height * 0.5);
    const float ox = float((projection[2][0] + projection[2][3]) * width * 0.5);
    const float oy = float((-projection[2][1] + projection[2][3]) * height * 0.5);
    vsg::vec3 horizontal, vertical;
    for (unsigned i = 0; i < 3; ++i)
    {
        horizontal[i] = float(modelview[i][0] * px + modelview[i][2] * ox);
        vertical[i] = float(modelview[i][1] * py + modelview[i][2] * oy);
    }
    const float normalization = 0.7071067811f / std::sqrt(vsg::dot(horizontal, horizontal) + vsg::dot(vertical, vertical));
    vsg::vec4 divisor;
    for (unsigned i = 0; i < 4; ++i)
        divisor[i] = float(modelview[i][2] * projection[2][3] + (i == 3 ? modelview[3][3] * projection[3][3] : 0.0)) * normalization;
    const vsg::vec4 center(float(sphere.x), float(sphere.y), float(sphere.z), 1.0f);
    return std::abs(float(sphere.r) / (center.x * divisor.x + center.y * divisor.y + center.z * divisor.z + divisor.w));
}
namespace
{
// 元数据和子节点由后台构造后只读；Record 仅更新 VSG 自带原子请求状态。
class SourceLOD : public vsg::Inherit<vsg::PagedLOD, SourceLOD>
{
public:
    explicit SourceLOD(const vsg::PagedLOD& source) : Inherit(source)
    {
        options = source.options; // 固定 VSG 的 PagedLOD 复制构造未复制 Options。
        setValue("osgb.sourceLOD", true);
    }
    const char* className() const noexcept override { return "vsg::PagedLOD"; }
    int compare(const vsg::Object& rhs) const override { return vsg::PagedLOD::compare(rhs); }
    vsg::ref_ptr<vsg::Object> clone(const vsg::CopyOp& copy) const override
    {
        auto result = SourceLOD::create(*this);
        for (unsigned i = 0; i < 2; ++i) result->children[i].node = copy(children[i].node);
        return result;
    }
    void accept(vsg::Visitor& visitor) override { visitor.apply(static_cast<vsg::PagedLOD&>(*this)); }
    void accept(vsg::ConstVisitor& visitor) const override { visitor.apply(static_cast<const vsg::PagedLOD&>(*this)); }
    void accept(vsg::RecordTraversal& visitor) const override
    {
        if (!visitor.state || !visitor.frameStamp) return;
        const auto* token = options ? options->getObject<OSGBPagingToken>(osgbPagingTokenKey) : nullptr;
        if (token && token->closed()) return;
        const auto frame = visitor.frameStamp->frameCount;
        const auto lastFine = frameHighResLastUsed.load();
        const bool recent = frame <= lastFine || frame - lastFine <= 1;
        auto inactive = [&](bool report) {
            if (report && visitor.culledPagedLODs) visitor.culledPagedLODs->highresCulled.emplace_back(this);
        };
        if (!visitor.state->intersect(bound)) { inactive(!recent); return; }
        double split = 0.0;
        const bool pixels = getValue("osgb.pixelSplit", split);
        if (!pixels) getValue("osgb.distanceSplit", split);
        vsg::dvec4 source(bound.x, bound.y, bound.z, bound.r);
        getValue(pixels ? "osgb.lodSphere" : "osgb.distanceSphere", source);
        const auto& projection = visitor.state->projectionMatrixStack.top();
        double height = token ? token->viewportHeight.load() : 1080.0;
        double width = height * std::abs(projection[1][1] / projection[0][0]);
        const auto& viewState = visitor.viewDependentState;
        if (viewState && viewState->view && viewState->view->camera && viewState->view->camera->viewportState &&
            !viewState->view->camera->viewportState->viewports.empty())
        {
            const auto& viewport = viewState->view->camera->viewportState->viewports.front();
            width = std::abs(viewport.width); height = std::abs(viewport.height);
        }
        double scale = viewState ? viewState->LODScale : 1.0;
        if (!(scale > 0.0) || !std::isfinite(scale)) scale = 1.0;
        double value = osgbRangeValue(vsg::dsphere(source.x, source.y, source.z, source.w), pixels,
            projection, visitor.state->modelviewMatrixStack.top(), width, height);
        value = pixels ? value / scale : value * scale;
        // OSG 的范围左闭右开：恰好达到 pixel split 选精模，恰好达到 distance split 选粗模。
        const bool fine = split > 0.0 && !std::isnan(value) && (pixels ? value >= split : value < split);
        if (fine)
        {
            const auto previous = frameHighResLastUsed.exchange(frame);
            if (visitor.culledPagedLODs && frame > previous && frame - previous > 1)
                visitor.culledPagedLODs->newHighresRequired.emplace_back(this);
            const auto importance = pixels ? value / split : split / std::max(value, 1e-9);
            const auto requested = std::clamp(importance, 1.0, 1000000.0);
            // 驻留精模也更新当前贡献，不能把旧视角的最高分永久留给预算调度。
            priority.store(requested);
            if (children[0].node) { children[0].node->accept(visitor); return; }
            if (visitor.databasePager)
            {
                // 自有 Pager 以请求状态 CAS 唯一准入；旧计数残留不得阻止后续帧恢复需求。
                // 其他宿主继续沿用既有计数门控，不改变其分页实现的调用约定。
                if (dynamic_cast<OSGBDatabasePager*>(visitor.databasePager.get()) || requestCount.fetch_add(1) == 0)
                    visitor.databasePager->request(vsg::ref_ptr<vsg::PagedLOD>(const_cast<SourceLOD*>(this)));
            }
        }
        else inactive(recent);
        // 精模未就绪时保留粗模；内存调度只延迟请求，不改变源范围判定。
        if (children[1].node) children[1].node->accept(visitor);
    }
};
struct Install : vsg::Visitor
{
    std::map<vsg::Node*, vsg::ref_ptr<vsg::Node>> visited;
    void replace(vsg::ref_ptr<vsg::Node>& node)
    {
        if (!node) return;
        const auto found = visited.find(node);
        if (found != visited.end()) { node = found->second; return; }
        auto original = node;
        auto page = node.cast<vsg::PagedLOD>();
        double split = 0;
        if (page && !dynamic_cast<SourceLOD*>(page.get()) &&
            (page->getValue("osgb.pixelSplit", split) || page->getValue("osgb.distanceSplit", split)))
            node = SourceLOD::create(*page);
        visited.emplace(original, node);
        node->accept(*this);
    }
    void apply(vsg::Node& node) override { node.traverse(*this); }
    void apply(vsg::Group& group) override { for (auto& child : group.children) replace(child); }
    void apply(vsg::QuadGroup& group) override { for (auto& child : group.children) replace(child); }
    void apply(vsg::PagedLOD& page) override { for (auto& child : page.children) replace(child.node); }
    void apply(vsg::LOD& lod) override { for (auto& child : lod.children) replace(child.node); }
    void apply(vsg::Switch& sw) override { for (auto& child : sw.children) replace(child.node); }
    void apply(vsg::CullNode& node) override { replace(node.child); }
};
}
vsg::ref_ptr<vsg::Node> installOSGBLOD(vsg::ref_ptr<vsg::Node> node)
{
    Install install;
    install.replace(node);
    return node;
}
}
