/**
 * rocky c++
 * Copyright 2025 Pelican Mapping
 * MIT License
 */
#pragma once
#include <geostudio/native/core/base/Common.h>
#include <geostudio/native/core/math/Math.h>

namespace geostudio::native
{
    //! Type of the VSG viewID
    using ViewIDType = std::uint32_t;
    using FrameCountType = std::uint64_t;

    //! State information at the time of rendering a view.
    struct RenderingState
    {
        ViewIDType viewID;
        FrameCountType frame;
        Rect viewport;
    };

    //! ViewLocal is a container that holds data on a per-view basis.
    template<typename T>
    struct ViewLocal : public std::array<T, GEOSTUDIO_NATIVE_MAX_NUMBER_OF_VIEWS>
    {
        //! default construct
        ViewLocal() = default;

        //! Construct with uniform value
        explicit ViewLocal(T v) { fill(v); };

        //! Set all views to a value (same as fill)
        ViewLocal<T>& operator = (T v) {
            this->fill(v);
            return *this;
        }
    };
}
