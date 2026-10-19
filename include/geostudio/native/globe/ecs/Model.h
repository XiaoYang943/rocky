/**
 * rocky c++
 * Copyright 2026 Pelican Mapping
 * MIT License
 */
#pragma once
#include <geostudio/native/globe/Common.h>
#include <geostudio/native/globe/URI.h>
#include <geostudio/native/globe/ecs/Component.h>
#include <optional>

namespace geostudio::native
{
    /**
    * Model component.
    * Represents a loadable 3D model.
    */
    struct Model : public Component<Model>
    {
        //! URI of model to load
        URI uri;

        //! Radius of model, once loaded
        float radius;

        //! Local matrix to apply to model after load, e.g. to rotate into an ENU frame
        std::optional<glm::dmat4> localMatrix;

        //! Failure information if the model failed to materialize.
        std::optional<Failure> error;
    };
}
