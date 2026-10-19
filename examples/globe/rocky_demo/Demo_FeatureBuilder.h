/**
 * rocky c++
 * Copyright 2023 Pelican Mapping
 * MIT License
 */
#pragma once
#include "helpers.h"

using namespace geostudio::native;

auto Demo_FeatureBuilder = [](Application& app)
    {
        static entt::entity graticule;
        static std::vector<entt::entity> tests;

        if (tests.empty())
        {
            auto&& [_, reg] = app.registry.write();

            // Let's make a simple graticule.
            // We need to separate meridians from paralles since the interpolation is different
            // (great circle vs rhumb line).

            Feature meridians;
            meridians.geometry.type = Geometry::Type::MultiLineString;
            meridians.interpolation = GeodeticInterpolation::GreatCircle;
            meridians.srs = SRS::WGS84;
            meridians.geometry.parts.reserve(36);
            for (int i = -180; i < 180; i += 10)
            {
                meridians.geometry.parts.emplace_back().points = {
                    { (double)i, -80.0, 0.0 },
                    { (double)i,  80.0, 0.0 },
                };
            }

            Feature parallels;
            parallels.geometry.type = Geometry::Type::MultiLineString;
            parallels.interpolation = GeodeticInterpolation::RhumbLine;
            parallels.srs = SRS::WGS84;
            parallels.geometry.parts.reserve(17);
            for (int i = -80; i <= 80; i += 10)
            {
                parallels.geometry.parts.emplace_back().points = {
                    { -180.0, (double)i, 0.0 },
                    {  -90.0, (double)i, 0.0 },
                    {    0.0, (double)i, 0.0 },
                    {   90.0, (double)i, 0.0 },
                    {  180.0, (double)i, 0.0 }
                };
            }

            graticule = reg.create();

            // A Geometry to hold the results. We have to use the "Segments"
            // topology since the data is not one continuous strip.
            auto& lineGeom = reg.emplace<LineGeometry>(graticule);
            lineGeom.topology = LineTopology::Segments;

            // And a style:
            auto& lineStyle = reg.emplace<LineStyle>(graticule);
            lineStyle.color = StockColor::Gray;
            lineStyle.depthOffset = 50000;
            lineStyle.resolution = 150000.0; // max segment length in meters (for tessellation)

            FeatureBuilder builder;

            builder.buildLineGeometry({ meridians, parallels }, lineStyle, lineGeom);

            // The Line ties it all together:
            reg.emplace<Line>(graticule, lineGeom, lineStyle);

            // Test: polygon feature that crosses the antimeridian.
            // See: a cyan rectangle spanning the 180th meridian from 10S to 10N
            {
                Feature poly;
                poly.geometry.type = Geometry::Type::Polygon;
                poly.srs = SRS::WGS84;
                poly.geometry.points = {
                    { 170.0, -10.0, 0.0 },
                    { -170.0, -10.0, 0.0 },
                    { -170.0,  10.0, 0.0 },
                    { 170.0,  10.0, 0.0 }
                };
                auto e = reg.create();
                auto& meshStyle = reg.emplace<MeshStyle>(e);
                meshStyle.color = StockColor::Aqua;
                meshStyle.depthOffset = 50000;
                FeatureBuilder builder;
                auto& meshGeom = reg.emplace<MeshGeometry>(e);
                builder.buildMeshGeometry({ poly }, meshStyle, meshGeom);
                reg.emplace<Mesh>(e, meshGeom, meshStyle);
                tests.emplace_back(e);
            }

            // Test: line feature that crosses the antimeridian
            // See: a purple line spanning the 180th meridian at 25S, using rhumb line interpolation
            {
                Feature line;
                line.geometry.type = Geometry::Type::LineString;
                line.interpolation = GeodeticInterpolation::RhumbLine;
                line.srs = SRS::WGS84;
                line.geometry.points = {
                    { 170.0, -25.0, 0.0 },
                    { -170.0, -25.0, 0.0 }
                };
                auto e = reg.create();
                auto& lineStyle = reg.emplace<LineStyle>(e);
                lineStyle.color = StockColor::Fuchsia;
                lineStyle.depthOffset = 50000;
                lineStyle.width = 5.0f;
                FeatureBuilder builder;
                auto& lineGeom = reg.emplace<LineGeometry>(e);
                //builder.buildLineGeometry({ line }, lineStyle, app.mapNode->srs(), lineGeom);
                builder.buildLineGeometry({ line }, lineStyle, lineGeom);
                reg.emplace<Line>(e, lineGeom, lineStyle);
                tests.emplace_back(e);
            }

            // Test: polar polygon in stereographic coordinates
            // See: a rectangle centered on the north pole
            {
                Feature poly;
                poly.geometry.type = Geometry::Type::Polygon;
                poly.srs = SRS("EPSG:3413"); // NS polar stereographic
                poly.geometry.points = {
                    { -500000.0, -500000.0, 0.0 },
                    {  500000.0, -500000.0, 0.0 },
                    {  500000.0,  500000.0, 0.0 },
                    { -500000.0,  500000.0, 0.0 }
                };
                auto e = reg.create();
                auto& meshStyle = reg.emplace<MeshStyle>(e);
                meshStyle.color = StockColor::Orange;
                meshStyle.depthOffset = 50000;
                FeatureBuilder builder;
                auto& meshGeom = reg.emplace<MeshGeometry>(e);
                builder.buildMeshGeometry({ poly }, meshStyle, meshGeom);
                reg.emplace<Mesh>(e, meshGeom, meshStyle);
                tests.emplace_back(e);
            }


            // Test: polar line geometry in stereographic coords
            // See: a red line crossing the south pole
            {
                Feature line;
                line.geometry.type = Geometry::Type::LineString;
                line.interpolation = GeodeticInterpolation::GreatCircle;
                line.srs = SRS("EPSG:3031"); // south polar stereographic
                line.geometry.points = {
                    { -500000.0, -500000.0, 0.0 },
                    {  500000.0,  500000.0, 0.0 }
                };
                auto e = reg.create();
                auto& lineStyle = reg.emplace<LineStyle>(e);
                lineStyle.color = StockColor::Red;
                lineStyle.depthOffset = 50000;
                lineStyle.width = 5.0f;
                FeatureBuilder builder;
                auto& lineGeom = reg.emplace<LineGeometry>(e);
                builder.buildLineGeometry({ line }, lineStyle, lineGeom);
                reg.emplace<Line>(e, lineGeom, lineStyle);
                tests.emplace_back(e);
            }


            // Test: a giant polygon that is more than 180 degrees wide
            {
                Feature poly;
                poly.geometry.type = Geometry::Type::Polygon;
                poly.interpolation = GeodeticInterpolation::RhumbLine;
                poly.srs = SRS::WGS84;
                poly.geometry.points = {
                    { -80.0, -25.0, 0.0 },
                    { -80.0, -30.0, 0.0 },
                    {  80.0, -30.0, 0.0 },
                    {  80.0, -25.0, 0.0 }
                };
                auto e = reg.create();
                auto& meshStyle = reg.emplace<MeshStyle>(e);
                meshStyle.color = StockColor::Lime;
                meshStyle.depthOffset = 50000;
                FeatureBuilder builder;
                auto& meshGeom = reg.emplace<MeshGeometry>(e);
                builder.buildMeshGeometry({ poly }, meshStyle, meshGeom);
                reg.emplace<Mesh>(e, meshGeom, meshStyle);
                tests.emplace_back(e);
            }

            app.vsgcontext->requestFrame();
        }

        ImGui::TextWrapped("%s", "FeatureBuilder is a helper utility for turning GIS feature data into geometry (lines and meshes).");

        ImGuiLTable::Begin("FeatureBuilder");
        static bool testsVisible = true;
        static bool graticuleVisible = true;

        if (ImGuiLTable::Checkbox("Show graticule", &graticuleVisible))
        {
            app.registry.write([&](entt::registry& reg)
                {
                    reg.get<Visibility>(graticule).visible.fill(graticuleVisible);
                });
        }

        if (ImGuiLTable::Checkbox("Show test features", &testsVisible))
        {
            app.registry.write([&](entt::registry& reg)
                {
                    for (auto e : tests)
                    {
                        reg.get<Visibility>(e).visible.fill(testsVisible);
                    }
                });
        }
        ImGuiLTable::End();
        
    };
