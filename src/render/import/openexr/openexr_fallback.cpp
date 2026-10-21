#include <geostudio/native/render/import/images/images.h>

using namespace geostudio::native::io;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// openEXR ReaderWriter fallback
//
openexr::openexr()
{
}

vsg::ref_ptr<vsg::Object> openexr::read(const vsg::Path&, vsg::ref_ptr<const vsg::Options>) const
{
    return {};
}

vsg::ref_ptr<vsg::Object> openexr::read(std::istream&, vsg::ref_ptr<const vsg::Options>) const
{
    return {};
}

vsg::ref_ptr<vsg::Object> openexr::read(const uint8_t*, size_t, vsg::ref_ptr<const vsg::Options>) const
{
    return {};
}

bool openexr::write(const vsg::Object*, const vsg::Path&, vsg::ref_ptr<const vsg::Options>) const
{
    return false;
}

bool openexr::write(const vsg::Object*, std::ostream&, vsg::ref_ptr<const vsg::Options>) const
{
    return false;
}

bool openexr::getFeatures(Features&) const
{
    return false;
}
