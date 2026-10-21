/**
 * rocky c++
 * Copyright 2023 Pelican Mapping
 * MIT License
 */
#pragma once

#include <geostudio/native/render/vsg/VSGContext.h>
#include <geostudio/native/render/entity/System.h>
#include <geostudio/native/render/entity/RenderTextureParticipant.h>
#include <geostudio/native/core/geo/SRS.h>
#include <geostudio/native/core/base/Callbacks.h>

namespace geostudio::native
{
    /**
    * ECS System that processes Transform and TransformDetail components.
    */
    class GEOSTUDIO_NATIVE_API TransformSystemNode :
        public vsg::Inherit<vsg::Node, TransformSystemNode>,
        public System,
        public RenderTextureParticipant
    {
    public:
        //! Construct the system
        TransformSystemNode(Registry& r);

        void update(VSGContext vsgcontext) override;

        //! Called periodically to update the transforms
        void traverse(vsg::RecordTraversal& record) const override;

        RenderTextureParticipant* renderTextureParticipant() override { return this; }
        vsg::Node* renderTextureNode() override { return this; }
        int renderTextureOrder() const override { return RenderTextureOrder::Transform; }

        //! Callback to invoke if the update/traverse resulted in any changes
        Callback<> onChanges;

    private:
        struct ViewDetail
        {
            SRS worldSRS;
        };

        // Per-view data, calculated during the record traversal
        mutable ViewLocal<ViewDetail> views;


        void on_construct_Transform(entt::registry& r, entt::entity e);
        void on_update_Transform(entt::registry& r, entt::entity e);
        void on_destroy_Transform(entt::registry& r, entt::entity e);
    };
}

EVSG_type_name(geostudio::native::TransformSystemNode)
