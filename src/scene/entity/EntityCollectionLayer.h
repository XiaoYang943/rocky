/**
 * rocky c++
 * Copyright 2025 Pelican Mapping
 * MIT License
 */
#pragma once

#include <geostudio/native/scene/layer/VisibleLayer.h>
#include <geostudio/native/scene/entity/Registry.h>

namespace geostudio::native
{
    /**
    * Layer that houses a collection of ECS entities.
    */
    class GEOSTUDIO_NATIVE_API EntityCollectionLayer : public Inherit<VisibleLayer, EntityCollectionLayer>
    {
    public:
        //! Entities managed by this layer
        std::vector<entt::entity> entities;

    public:
        //! Construct a new layer
        //! @param registry ECS Registry
        EntityCollectionLayer(Registry registry);

        //! Construct a new layer from serialized JSON
        //! @param registry ECS Registry
        EntityCollectionLayer(Registry registry, std::string_view JSON);

    protected:

        Result<> openImplementation(const IOOptions& io) override;

        void closeImplementation() override;

    protected:

        Registry _registry;

    private:

        void construct(std::string_view JSON);
    };
}
