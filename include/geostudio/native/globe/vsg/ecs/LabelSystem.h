/**
 * rocky c++
 * Copyright 2023 Pelican Mapping
 * MIT License
 */
#pragma once
#include <geostudio/native/globe/ecs/Label.h>
#include <geostudio/native/globe/ecs/Widget.h>
#include <geostudio/native/globe/Rendering.h>
#include <geostudio/native/globe/vsg/ecs/ECSNode.h>

#ifdef ROCKY_HAS_IMGUI
#include <geostudio/native/globe/rocky_imgui.h>

namespace geostudio::native
{
    /**
     * System that renders Label components
     */
    class ROCKY_EXPORT LabelSystem : public geostudio::native::Inherit<System, LabelSystem>
    {
    public:
        //! Construct the mesh renderer
        LabelSystem(Registry& registry);

        //! One time setup of the system
        void initialize(VSGContext) override;

        //! Per frame update
        void update(VSGContext) override;

    protected:
        struct ImFontContainer {
            ImFont* font = nullptr;
        };
        using FontByName = std::unordered_map<std::string, ImFontContainer>;
        using FontSetPerContext = std::unordered_map<ImGuiContext*, FontByName>;
        FontSetPerContext _fontsCache;

        std::function<void(WidgetInstance&)> _renderFunction;
        entt::entity _defaultStyleEntity = entt::null;

        void on_construct_Label(entt::registry& r, entt::entity e);
        void on_update_Label(entt::registry& r, entt::entity e);
        void on_destroy_Label(entt::registry& r, entt::entity e);
        void on_construct_LabelStyle(entt::registry& r, entt::entity e);
        void on_update_LabelStyle(entt::registry& r, entt::entity e);
        void on_destroy_LabelStyle(entt::registry& r, entt::entity e);

        ImFont* getOrCreateFont(const std::string&, ImGuiContext*);
    };
}

#endif // ROCKY_HAS_IMGUI