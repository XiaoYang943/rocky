#include <geostudio/native/globe/vsg/ecs/slug/SlugAdapter.h>
#include <filesystem>
#include <iostream>
#include <utility>

// PRIVATE static linking must not leak Slughorn's cxx_std_20 usage requirement.
#ifdef _MSVC_LANG
static_assert(_MSVC_LANG == 201703L, "Slughorn raised the consumer's C++ standard");
#else
static_assert(__cplusplus == 201703L, "Slughorn raised the consumer's C++ standard");
#endif

int main()
{
    geostudio::native::detail::SlugShapeInput shape;
    shape.kind = geostudio::native::detail::SlugShapeKind::Fill;
    geostudio::native::detail::SlugPolygonInput polygon;
    polygon.outer.closed = true;
    polygon.outer.points = { {0.1f, 0.1f}, {0.9f, 0.1f}, {0.9f, 0.9f}, {0.1f, 0.9f} };
    shape.polygons.emplace_back(std::move(polygon));

    geostudio::native::detail::SlugAtlasInput input;
    input.shapes.emplace_back(std::move(shape));
    input.exportPath = "slughorn-dependency-smoke.slug";
    geostudio::native::detail::SlugAtlasOutput output;
    std::string error;
    if (!geostudio::native::detail::buildSlugAtlas(input, output, error) ||
        output.layers.size() != 1 || output.bandTexture.bytes.empty() ||
        output.curveTexture.bytes.empty() || !output.exportSucceeded ||
        !std::filesystem::exists(input.exportPath))
    {
        std::cerr << error << '\n' << output.exportMessage << '\n';
        return 1;
    }
    std::filesystem::remove(input.exportPath);
    return 0;
}
