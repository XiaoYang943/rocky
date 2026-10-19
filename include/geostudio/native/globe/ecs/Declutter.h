/**
 * rocky c++
 * Copyright 2025 Pelican Mapping
 * MIT License
 */
#pragma once
#include <geostudio/native/globe/Common.h>
#include <geostudio/native/globe/Math.h>

namespace geostudio::native
{
    /**
    * ECS component whose precense on an entity indicates that the entity
    * should particpate in decluttering.
    */
    struct Declutter
    {
        float priority = 0.0f;
        Rect rect;
    };
}
