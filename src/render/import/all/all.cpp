/* <editor-fold desc="MIT License">

Copyright(c) 2021 Robert Osfield

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <geostudio/native/render/import/3DTiles/3DTiles.h>
#include <geostudio/native/render/import/all/all.h>
#include <geostudio/native/render/import/bin/bin.h>
#include <geostudio/native/render/import/cpp/cpp.h>
#include <geostudio/native/render/import/curl/curl.h>
#include <geostudio/native/render/import/freetype/freetype.h>
#include <geostudio/native/render/import/gdal/gdal.h>
#include <geostudio/native/render/import/gltf/gltf.h>
#include <geostudio/native/render/import/images/images.h>
#include <geostudio/native/render/import/all/models.h>
#include <geostudio/native/render/import/assimp/assimp.h>
#include <geostudio/native/render/import/osgb/OSGB.h>

#include <vsg/io/Logger.h>
#include <vsg/io/VSG.h>
#include <vsg/io/glsl.h>
#include <vsg/io/json.h>
#include <vsg/io/spirv.h>
#include <vsg/io/txt.h>

#ifdef vsgXchange_OSG
#    include <osg2vsg/OSG.h>
#endif

using namespace geostudio::native::io;

void geostudio::native::io::init()
{
    static bool s_vsgXchange_initialized = false;
    if (s_vsgXchange_initialized) return;
    s_vsgXchange_initialized = true;

    vsg::debug("geostudio::native::io::init()");

    vsg::ObjectFactory::instance()->add<geostudio::native::io::all>();
    vsg::ObjectFactory::instance()->add<geostudio::native::io::curl>();

    vsg::ObjectFactory::instance()->add<geostudio::native::io::cpp>();

    vsg::ObjectFactory::instance()->add<geostudio::native::io::stbi>();
    vsg::ObjectFactory::instance()->add<geostudio::native::io::dds>();
    vsg::ObjectFactory::instance()->add<geostudio::native::io::ktx>();

    vsg::ObjectFactory::instance()->add<geostudio::native::io::bin>();
    vsg::ObjectFactory::instance()->add<geostudio::native::io::OSGB>();
    vsg::ObjectFactory::instance()->add<geostudio::native::io::gltf>();
    vsg::ObjectFactory::instance()->add<geostudio::native::io::Tiles3D>();

    vsg::ObjectFactory::instance()->add<geostudio::native::io::openexr>();
    vsg::ObjectFactory::instance()->add<geostudio::native::io::freetype>();
    vsg::ObjectFactory::instance()->add<geostudio::native::io::assimp>();
    vsg::ObjectFactory::instance()->add<geostudio::native::io::GDAL>();
    // Read legacy serialized Reader types while newly written objects use the SDK namespace.
    auto& creators = vsg::ObjectFactory::instance()->getCreateMap();
    for (const char* type : {"all", "curl", "cpp", "stbi", "dds", "ktx", "bin", "gltf", "Tiles3D", "openexr", "freetype", "assimp", "GDAL"})
    {
        creators[std::string("vsgXchange::") + type] = creators.at(std::string("geostudio::native::io::") + type);
    }
}

all::all()
{
    // for convenience make sure the init() method is called
    geostudio::native::io::init();

#ifdef vsgXchange_curl
    add(curl::create());
#endif

    add(vsg::VSG::create());
    add(vsg::spirv::create());
    add(vsg::glsl::create());

    add(bin::create());
    add(OSGB::create());
    add(gltf::create());
    add(Tiles3D::create());

    add(vsg::json::create());

    add(cpp::create());

    add(vsg::txt::create());

    add(stbi::create());
    add(dds::create());

#ifdef vsgXchange_ktx
    add(ktx::create());
#endif

#ifdef vsgXchange_openexr
    add(openexr::create());
#endif

#ifdef vsgXchange_freetype
    add(freetype::create());
#endif

#ifdef vsgXchange_assimp
    add(assimp::create());
#endif

#ifdef vsgXchange_GDAL
    add(GDAL::create());
#endif

#ifdef vsgXchange_OSG
    add(osg2vsg::OSG::create());
#endif
}
