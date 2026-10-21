/**
 * rocky c++
 * Copyright 2026 Pelican Mapping
 * MIT License
 */
#pragma once
#include <geostudio/native/scene/entity/Component.h>

namespace geostudio::native
{
    /**
    * Component that works with a Transform to dynamically constrain
    * the draw size of an object to a range of pixel sizes on screen.
    */
    struct PixelScale
    {
        //! Whether to consider pixel scale even when this component is present
        bool enabled = true;

        //! Minimum pixel size of the object on screen. If the object appears smaller than this, it will be scaled up.
        float minPixels = 16.0f;

        //! Maximum pixel size of the object on screen. If the object appears larger than this, it will be scaled down.
        float maxPixels = 256.0f;
    };
}
