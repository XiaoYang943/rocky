/**
 * rocky c++
 * Copyright 2023 Pelican Mapping
 * MIT License
 */
#pragma once

#include <geostudio/native/globe/ecs/Registry.h>
#include <geostudio/native/globe/ecs/Declutter.h>
#include <geostudio/native/globe/vsg/ecs/DeclutterSystem.h>
#include "helpers.h"

using namespace geostudio::native;

using namespace std::chrono_literals;

namespace
{
    void resetVisibility(Registry r)
    {
        auto [lock, registry] = r.read();

        auto view = registry.view<Declutter, Visibility>();
        for (auto&& [entity, declutter, visibility] : view.each())
        {
            visibility.visible = true;
        }
    }
}

auto Demo_Decluttering = [](Application& app)
{
    static std::shared_ptr<DeclutterSystem> declutter;
    static float updateHertz = 1.0f;
    static bool declutteringEnabled = true;

    if (!declutter)
    {
        declutter = DeclutterSystem::create(app.registry);

        app.workers.start("geostudio::native::declutter", app.io(), [&app]()
            {
                run_at_frequency f(updateHertz);
                if (declutteringEnabled)
                {
                    declutter->update(app.vsgcontext);
                    app.vsgcontext->requestFrame();
                }
                return true;
            });

        app.vsgcontext->requestFrame();
    }

    if (ImGuiLTable::Begin("declutter"))
    {
        if (ImGuiLTable::Checkbox("Enabled", &declutteringEnabled)) {
            if (!declutteringEnabled)
                resetVisibility(app.registry);
        }

        static const char* sorting[] = { "Priority", "Distance" };
        ImGuiLTable::Combo("Sort by", (int*)&declutter->sorting, sorting, 2);

        ImGuiLTable::SliderFloat("Buffer", &declutter->bufferPixels, -25.0f, 50.0f, "%.0f px");
        ImGuiLTable::SliderFloat("Frequency", &updateHertz, 1.0f, 30.0f, "%.0f hz");

        if (declutteringEnabled)
        {
            auto [visible, total] = declutter->visibleAndTotal();
            ImGuiLTable::Text("Results:", "%ld visible / %ld total", visible, total);
        }

        ImGuiLTable::End();
    }
};
