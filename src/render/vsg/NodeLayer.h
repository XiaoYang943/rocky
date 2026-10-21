/**
 * rocky c++
 * Copyright 2025 Pelican Mapping
 * MIT License
 */
#pragma once
#include <geostudio/native/scene/layer/VisibleLayer.h>
#include <geostudio/native/render/vsg/VSGContext.h>

namespace geostudio::native
{
    /**
    * NodeLayer enscapulates a VSG Node in a Rocky map layer.
    */
    class GEOSTUDIO_NATIVE_API NodeLayer : public Inherit<geostudio::native::VisibleLayer, NodeLayer>
    {
    public:
        //! Default construct
        NodeLayer() = default;

        //! Construct with a node
        NodeLayer(vsg::ref_ptr<vsg::Node> in_node) :
            super(), node(in_node) {
        }

        vsg::ref_ptr<vsg::Node> node;

    protected:

        Result<> openImplementation(const IOOptions& io) override;

        void closeImplementation() override;
    };
}
