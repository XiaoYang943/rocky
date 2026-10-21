/**
 * rocky c++
 * Copyright 2025 Pelican Mapping
 * MIT License
 */
#include <geostudio/native/render/vsg/NodeLayer.h>
#include <geostudio/native/render/vsg/VSGUtils.h>
#include <geostudio/native/render/entity/EntityNode.h>
#include <geostudio/native/scene/entity/Visibility.h>

using namespace geostudio::native;
using namespace geostudio::native::detail;

Result<> NodeLayer::openImplementation(const IOOptions& io)
{
    forEach<EntityNode>(node, [&](EntityNode* entityNode)
        {
            auto [lock, r] = entityNode->registry.write();
            for (auto e : entityNode->entities)
                r.emplace_or_replace<ActiveState>(e);
        });

    return ResultVoidOK;
}

void NodeLayer::closeImplementation()
{
    forEach<EntityNode>(node, [&](EntityNode* entityNode)
        {
            auto [lock, r] = entityNode->registry.write();
            r.remove<ActiveState>(entityNode->entities.begin(), entityNode->entities.end());
        });
}
