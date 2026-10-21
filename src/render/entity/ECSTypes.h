/**
 * rocky c++
 * Copyright 2025 Pelican Mapping
 * MIT License
 */
#pragma once

#include <geostudio/native/render/vsg/Common.h>
#include <geostudio/native/scene/entity/Component.h>
#include <geostudio/native/render/entity/Texture.h>

namespace geostudio::native
{
    /** ECS component that holds a VSG node */
    struct NodeGraph : public Component<NodeGraph>
    {
        vsg::ref_ptr<vsg::Node> node;
    };
}
