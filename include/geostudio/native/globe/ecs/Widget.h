/**
 * rocky c++
 * Copyright 2023 Pelican Mapping
 * MIT License
 */
#pragma once
#include <geostudio/native/globe/Common.h>
#include <geostudio/native/globe/Rendering.h>
#include <geostudio/native/globe/ecs/Highlight.h>
#if defined(ROCKY_HAS_IMGUI) && __has_include(<imgui.h>)
#include <imgui.h>
#include <entt/entt.hpp>

namespace geostudio::native
{
    struct WidgetInstance;

    /**
    * Widget ECS component
    */
    struct Widget
    {
        //! Render function
        std::function<void(WidgetInstance&)> render;
    };

    //! Structure that is passed to the Widget custom render function, giving
    //! you access to a variety of information and convenience functions.
    struct WidgetInstance
    {
        struct Widget& widget;
        const std::string& uid;
        entt::registry& registry;
        const RenderingState& view;
        entt::entity entity;
        int windowFlags;
        ImVec2 position;
        ImGuiContext* context;
        bool hasFocus = false;

        inline bool checkFocus() {
            auto min = ImGui::GetWindowPos();
            auto winSize = ImGui::GetWindowSize();
            auto mouse = ImGui::GetMousePos();
            hasFocus =
                mouse.x >= min.x && mouse.x < (min.x + winSize.x) &&
                mouse.y >= min.y && mouse.y < (min.y + winSize.y);
            return hasFocus;
        }

        inline bool drawHighlight() {
            bool highlighted = false;
            if (auto* highlight = registry.try_get<Highlight>(entity))
            {
                auto min = ImGui::GetWindowPos();
                auto winSize = ImGui::GetWindowSize();
                ImGui::GetWindowDrawList()->AddRectFilled(
                    min,
                    ImVec2(min.x + winSize.x, min.y + winSize.y),
                    highlight->color.as(Color::Format::ABGR),
                    ImGui::GetStyle().WindowRounding);
                highlighted = true;
            }
            return highlighted;
        }
    };
}

#endif
