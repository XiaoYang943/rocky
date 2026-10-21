/**
 * rocky c++
 * Copyright 2025 Pelican Mapping
 * MIT License
 */
#pragma once
#include <geostudio/native/data/raster/Color.h>
#include <geostudio/native/core/geo/SRS.h>
#include <geostudio/native/scene/entity/Component.h>
#include <vector>

namespace geostudio::native
{
    struct GEOSTUDIO_NATIVE_API PointStyle : public Component<PointStyle>
    {
        Color color = StockColor::White; // used when useGeometryColors == false
        float width = 3.0f; // pixels; used then useGeometryWidths = false
        float antialias = 0.25f;
        float depthOffset = 0.0f; // meters
        bool useGeometryColors = false;
        bool useGeometryWidths = false;
        bool transparencyBin = false;
    };


    struct GEOSTUDIO_NATIVE_API PointGeometry : public Component<PointGeometry>
    {
        //! SRS of the points in the points vector (when set)
        SRS srs;

        //! Points can be absolute (in the world SRS), relative to a topocentric
        //! coordinate system (if a topocentric Transform is in use), or in the SRS of the 
        //! referencePoint if that is in use.
        std::vector<glm::dvec3> points;

        //! Colors per point (optional). These apply when this geometry is coupled with a 
        //! style that has useGeometryColors = true.
        std::vector<Color> colors;

        //! Widths per point (optional). These apply when this geometry is coupled with a 
        //! style that has useGeometryWidths = true.
        std::vector<float> widths;

        //! reset this geometry for reuse.
        void recycle(entt::registry&);
    };


    /**
    * Point(s) component - holds a collection of points
    */
    class GEOSTUDIO_NATIVE_API Point : public Component<Point>
    {
    public:
        //! Entity holding the PointStyle to use
        entt::entity style = entt::null;

        //! Entity holding the PointGeometry to use
        entt::entity geometry = entt::null;

        //! Useful constructors
        inline Point() = default;
        inline Point(const PointGeometry& geometry_) : geometry(geometry_.owner) {}
        inline Point(const PointGeometry& geometry_, const PointStyle& style_) : geometry(geometry_.owner), style(style_.owner) {}
    };
}
