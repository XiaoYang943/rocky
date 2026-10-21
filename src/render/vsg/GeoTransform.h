/**
 * rocky c++
 * Copyright 2023 Pelican Mapping
 * MIT License
 */
#pragma once

#include <geostudio/native/render/vsg/Common.h>
#include <geostudio/native/scene/entity/Transform.h>
#include <geostudio/native/render/entity/TransformDetail.h>

namespace geostudio::native
{
    /**
     * Transform node that accepts geospatial coordinates and creates
     * a local ENU (X=east, Y=north, Z=up) coordinate frame for its children
     * that is tangent to the earth at the transform's geo position.
     */
    class GEOSTUDIO_NATIVE_API GeoTransform : public vsg::Inherit<vsg::Group, GeoTransform>,
        public Transform
    {
    public:
        //! Construct an invalid geotransform
        GeoTransform() = default;

    public:

        //! Disables the copy constructor.
        GeoTransform(const GeoTransform& rhs) = delete;

        void traverse(vsg::RecordTraversal&) const override;

    private:

        mutable detail::TransformDetail _transformDetail;
    };

} // namespace

EVSG_type_name(geostudio::native::GeoTransform)
