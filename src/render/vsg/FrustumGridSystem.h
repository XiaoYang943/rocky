/**
 * rocky c++
 * Copyright 2026 Pelican Mapping
 * MIT License
 */
#pragma once

#include <geostudio/native/render/vsg/VSGContext.h>
#include <geostudio/native/render/entity/ECSNode.h>

namespace geostudio::native
{
    /**
    * System that emits a screen-space frustum grid to the GPU for use with
    * tile-based culling of lights, decals, etc.
    */
    class GEOSTUDIO_NATIVE_API FrustumGridSystemNode : public vsg::Inherit<detail::SimpleSystemNodeBase, FrustumGridSystemNode>
    {
    public:
        //! Construct the system
        FrustumGridSystemNode(Registry& r);

        void initialize(VSGContext vsgcontext) override;
        void update(VSGContext vsgcontext) override;
        void traverse(vsg::RecordTraversal& record) const override;

    private:
        struct Grid
        {
            ViewIDType viewID;
            vsg::vec4 viewport;
            vsg::mat4 projection;
        };

        struct ViewDetail
        {
            vsg::dmat4 lastProjMatrix;
            vsg::ref_ptr<vsg::Commands> commands;
            std::optional<Grid> newGrid;
        };

        // Per-view data, calculated during the record traversal
        mutable ViewLocal<ViewDetail> _views;
        mutable std::uint64_t _lastFrameCount = ~0U;
        vsg::ref_ptr<vsg::ShaderStage> _shader;
        std::shared_ptr<SharedRenderData> _sharedRenderData;

    };
}

EVSG_type_name(geostudio::native::FrustumGridSystemNode)