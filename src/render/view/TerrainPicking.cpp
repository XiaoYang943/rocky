/**
 * rocky c++
 * Copyright 2026 Pelican Mapping
 * MIT License
 */
#include <geostudio/native/render/view/DisplayManager.h>
#include <geostudio/native/render/terrain/TerrainNode.h>

using namespace geostudio::native;

Result<TerrainIntersection>
geostudio::native::geoPointAtWindowCoords(View& view, int x, int y)
{
    GEOSTUDIO_NATIVE_SOFT_ASSERT_AND_RETURN(view, Failure_AssertionFailure);
    GEOSTUDIO_NATIVE_SOFT_ASSERT_AND_RETURN(view.vsgView->camera, Failure_AssertionFailure);

    auto terrain = detail::find<TerrainNode>(view.vsgView);
    if (!terrain)
        return Failure_AssertionFailure;

    vsg::LineSegmentIntersector lsi(*view.vsgView->camera, x, y);
    terrain->accept(lsi);
    if (lsi.intersections.empty())
        return Failure{};

    auto closest = std::min_element(
        lsi.intersections.begin(), lsi.intersections.end(),
        [](const auto& lhs, const auto& rhs) { return lhs->ratio < rhs->ratio; });

    // calcluate the normal vector at the intersection point:
    auto verts = closest->get()->arrays.front()->cast<vsg::vec3Array>();
    auto& indices = closest->get()->indexRatios;
    vsg::vec3 normal = vsg::normalize(vsg::cross(
        verts->at(indices[1].index) - verts->at(indices[0].index),
        verts->at(indices[2].index) - verts->at(indices[0].index)));

    // transform the normal into world space:
    auto worldNormal = glm::dmat3(to_glm(closest->get()->localToWorld)) * glm::dvec3(to_glm(normal));

    TerrainIntersection result;
    result.point = GeoPoint(terrain->renderingSRS, closest->get()->worldIntersection);
    result.normal = glm::normalize(worldNormal);

    return result;
}


std::tuple<Result<TerrainIntersection>, View>
geostudio::native::geoPointAtWindowCoords(Window& window, int x, int y)
{
    GEOSTUDIO_NATIVE_SOFT_ASSERT_AND_RETURN(window, std::make_tuple(Failure_AssertionFailure, View()));

    for (auto& view : window.views())
    {
        if (view.vsgView->camera)
        {
            const auto vp = view.vsgView->camera->getViewport();

            if (x >= vp.x && x < vp.x + vp.width && y >= vp.y && y < vp.y + vp.height)
            {
                if (auto p = geoPointAtWindowCoords(view, x, y))
                {
                    return std::make_tuple(p, view);
                }
            }
        }
    }

    return std::make_tuple(Failure{}, View());
}
