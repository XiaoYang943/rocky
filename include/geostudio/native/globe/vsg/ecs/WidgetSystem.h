/**
 * rocky c++
 * Copyright 2025 Pelican Mapping
 * MIT License
 */
#pragma once

#include <geostudio/native/globe/vsg/VSGContext.h>

#if defined(ROCKY_HAS_IMGUI)
#include <geostudio/native/globe/vsg/ecs/System.h>
#include <geostudio/native/globe/ecs/Registry.h>
#include <geostudio/native/globe/Rendering.h>
#include <unordered_set>

namespace geostudio::native
{
    /**
     * Creates commands for rendering ImGui-based overlays
     */
    class WidgetSystemNode : public vsg::Inherit<vsg::Node, WidgetSystemNode>,
        public System
    {
    public:
        //! Construct the mesh renderer
        WidgetSystemNode(Registry& registry);

        //! One time setup of the system
        void initialize(VSGContext context) override;

        //! Intersection support
        void traverse(vsg::ConstVisitor& v) const override;

    private:

        // Each view records hover state in its own ImGui context.
        ViewLocal<std::unordered_set<entt::entity>> _focusedEntities;


        void on_construct_Widget(entt::registry& r, entt::entity e);
        void on_destroy_Widget(entt::registry& r, entt::entity e);
    };
}

EVSG_type_name(geostudio::native::WidgetSystemNode)
#endif // defined(ROCKY_HAS_IMGUI)
