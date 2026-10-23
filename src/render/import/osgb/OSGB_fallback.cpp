#include <geostudio/native/render/import/osgb/OSGB.h>
#include <stdexcept>
using namespace geostudio::native::io;
struct OSGB::Impl {};
OSGB::OSGB() = default;
OSGB::~OSGB() = default;
vsg::ref_ptr<vsg::Object> OSGB::read(const vsg::Path&, vsg::ref_ptr<const vsg::Options>) const { return {}; }
std::future<vsg::ref_ptr<vsg::Node>> OSGB::readAsync(const vsg::Path&, vsg::ref_ptr<const vsg::Options>)
{
    std::promise<vsg::ref_ptr<vsg::Node>> result; result.set_value({}); return result.get_future();
}
bool OSGB::getFeatures(Features&) const { return false; }
void OSGB::close() {}
void OSGB::configure(const Settings&) {}
void OSGB::enablePageCache(bool) {}
OSGB::Performance OSGB::performance() const { return {}; }
void OSGB::updateView(vsg::Node*, double, double) {}
OSGB::BuildResult OSGB::buildDataset(const vsg::Path&, const BuildSettings&) { throw std::runtime_error("OSGB support is disabled in this SDK build"); }
std::string OSGB::error() const { return "OSGB support is disabled in this SDK build"; }
OSGB::Status OSGB::status(const vsg::Node*, uint64_t) const { Status value; value.error = error(); return value; }
bool OSGB::available() { return false; }
