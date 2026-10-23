#pragma once
#include <vsg/all.h>
#include <set>

// CPU 统计驻留几何、纹理与外部页；不触发分页、编译或 GPU 分配。
struct OSGBStats : vsg::ConstVisitor
{
    size_t draws = 0, textures = 0;
    std::vector<const vsg::PagedLOD*> pages;
    std::vector<vsg::ref_ptr<vsg::GraphicsPipeline>> pipelines;
    std::set<const vsg::Object*> seen;
    void apply(const vsg::Object& object) override { if (seen.insert(&object).second) object.traverse(*this); }
    void apply(const vsg::VertexIndexDraw& object) override { ++draws; object.traverse(*this); }
    void apply(const vsg::DrawIndexed&) override { ++draws; }
    void apply(const vsg::Draw&) override { ++draws; }
    void apply(const vsg::BindGraphicsPipeline& object) override { pipelines.push_back(object.pipeline); }
    void apply(const vsg::DescriptorImage& object) override
    {
        for (auto& image : object.imageInfoList)
            if (image && image->imageView && image->imageView->image && image->imageView->image->data) ++textures;
    }
    void apply(const vsg::PagedLOD& object) override { pages.push_back(&object); object.traverse(*this); }
};
