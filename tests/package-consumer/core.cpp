#include <geostudio/native/core/geo/SRS.h>
#include <geostudio/native/data/elevation/Heightfield.h>
#include <geostudio/native/data/vector/Feature.h>
#include <cmath>

#if defined(GLM_FORCE_XYZW_ONLY) || defined(GEOSTUDIO_NATIVE_HAS_IMGUI)
#error Private backend configuration escaped into the Core/Data consumer
#endif

static_assert(GEOSTUDIO_NATIVE_VERSION_AT_LEAST(0, 3, 0), "SDK version contract");

// 独立消费 Core/Data 的计算能力，不链接 Runtime 或创建图形对象。
int main()
{
    using namespace geostudio::native;
    Heightfield height(2, 2);
    for (unsigned y = 0; y < 2; ++y)
        for (unsigned x = 0; x < 2; ++x)
            height.heightAt(x, y) = 42.0f;
    glm::dvec3 world;
    if (!SRS::WGS84.to(SRS::ECEF).transform(glm::dvec3(0, 0, 0), world))
        return 1;
    Feature feature;
    feature.fields["id"].emplace<std::int64_t>(42);
    return std::abs(world.x - 6378137.0) < 0.001 &&
        height.heightAtUV(0.5f, 0.5f) == 42.0f && feature.field("id").intValue() == 42 ? 0 : 1;
}
