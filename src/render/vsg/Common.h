/**
 * rocky c++
 * Copyright 2026 Pelican Mapping
 * MIT License
 */
#pragma once

#include <geostudio/native/core/base/Common.h>
#include <vsg/all.h>


// non-exported classes
#define GEOSTUDIO_NATIVE_VSG_INTERNAL

#ifndef VSG_COMPUTE_VERSION
#define VSG_COMPUTE_VERSION(major, minor, rev) ((major) * 10000 + (minor) * 100 + (rev))
#define VSG_VERSION_INTEGER VSG_COMPUTE_VERSION(VSG_VERSION_MAJOR, VSG_VERSION_MINOR, VSG_VERSION_PATCH)
#endif

#if VSG_API_VERSION_LESS(1, 1, 12)
#error "Rocky requires VSG 1.1.12 or later"
#endif

namespace geostudio::native
{
    //! Marker class for the disposal interface
    class ObjectLifecycle
    {
    public:
        virtual void dispose(vsg::ref_ptr<vsg::Object>) = 0;
        virtual vsg::CompileResult compile(vsg::ref_ptr<vsg::Object>) = 0;
    };
}
