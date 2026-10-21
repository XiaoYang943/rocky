/**
 * rocky c++
 * Copyright 2026 Pelican Mapping
 * MIT License
 */
#pragma once
#include <geostudio/native/streaming/terrain/ElevationSampler.h>
#include <geostudio/native/data/vector/Feature.h>
#include <geostudio/native/scene/entity/Line.h>
#include <geostudio/native/scene/entity/Mesh.h>
#include <geostudio/native/scene/entity/Polygon.h>
#include <vector>

namespace geostudio::native
{
    /**
    * FeatureBuilder is a utility that takes a collection of Feature objects
    * and populates (appends to) a geometry component.
    */
    class GEOSTUDIO_NATIVE_API FeatureBuilder
    {
    public:

        //! Elevation sampler (optional) will clamp geometry to the ground.
        ElevationSession clamper;

        //! Function that returns a color for a feature (experimental - may change)
        std::function<Color(const Feature&)> colorFunction;

    public: // advanced options

        //! Output SRS. You should rarely change this. The default of WGS84 is
        //! the best choice for supporting multiple views with multiple SRSs.
        //! If you KNOW you will only ever use one SRS in all your views, you 
        //! can safely change this to match that view's SRS for a small
        //! performance boost.
        SRS outputSRS = SRS::WGS84;

        //! Reference point (optional) to use for geometry localization.
        //! Do not use this if you are using the default outputSRS.
        //! If you do set this, make sure to add a corresponding Transform component
        //! to each of the resulting entities.
        GeoPoint origin;

    public:

        //! Default constructor
        FeatureBuilder() = default;
        
        //! Append line geometry for a collection of features.
        void buildLineGeometry(const std::vector<Feature>& features, const LineStyle& style,
            LineGeometry& lineGeom);

        //! Append mesh geometry for a collection of features.
        void buildMeshGeometry(const std::vector<Feature>& features, const MeshStyle& style,
            MeshGeometry& meshGeom);

        //! Append polygon geometry while preserving exterior rings and holes.
        void buildPolygonGeometry(const std::vector<Feature>& features,
            const PolygonStyle& style, PolygonGeometry& polygonGeom);

        //! Append a triangulated mesh derived from polygon geometry.
        void buildMeshGeometry(const PolygonGeometry& polygons,
            const PolygonStyle& style, MeshGeometry& meshGeom);
    };
}
