/**
 * rocky c++
 * Copyright 2025 Pelican Mapping
 * MIT License
 */
#pragma once

#include <geostudio/native/globe/vsg/Common.h>
#include <geostudio/native/globe/ecs/Component.h>
#include <geostudio/native/globe/vsg/ecs/Texture.h>

namespace geostudio::native
{
    /** ECS component that holds a VSG node */
    struct NodeGraph : public Component<NodeGraph>
    {
        vsg::ref_ptr<vsg::Node> node;
    };
}
