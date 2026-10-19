/**
 * rocky c++
 * Copyright 2026 Pelican Mapping
 * MIT License
 */
#include <geostudio/native/globe/vsg/Application.h>
#include <geostudio/native/globe/vsg/ShaderDefines.h>
#include <geostudio/native/globe/vsg/ecs/DecalSystem.h>
#include "helpers.h"

using namespace geostudio::native;

auto Demo_FrustumGrid = [](Application& app)
{
    ImGuiLTable::Begin("frustum_grid_demo");

    auto vds = app.vsgcontext->sharedRenderData->viewDependentState[0];
    if (!vds || !vds->frustumParamsBuf)
    {
        ImGuiLTable::End();
        return;
    }
    BufferAccess<FrustumGridParamsGPU> params(vds->frustumParamsBuf);
    float debug = params->debugTiles;

    if (ImGuiLTable::SliderFloat("Show frustum grid", &debug, 0.0f, 1.0f))
    {
        for(ViewIDType viewID = 0; viewID < ROCKY_MAX_NUMBER_OF_VIEWS; ++viewID)
        {
            auto vds = app.vsgcontext->sharedRenderData->viewDependentState[viewID];
            if (vds) {
                BufferAccess<FrustumGridParamsGPU> params(vds->frustumParamsBuf);
                params->debugTiles = std::clamp(debug, 0.0f, 1.0f);
                app.vsgcontext->upload(params);
            }
        }
    }

#ifdef ROCKY_HAS_DECALS
    if (auto* decals = app.computeSystemsNode->get<DecalSystemNode>())
    {
        if (ImGuiLTable::Checkbox("Show decal volumes", &decals->debugVolumes))
            app.vsgcontext->requestFrame();
        if (decals->debugVolumes &&
            ImGuiLTable::Checkbox("See through terrain", &decals->debugVolumesSeeThrough))
            app.vsgcontext->requestFrame();
    }
#endif

    ImGuiLTable::End();
};
