/**
 * rocky c++
 * Copyright 2023 Pelican Mapping
 * MIT License
 */
#include <geostudio/native/globe/vsg/Application.h>
#include "helpers.h"
#include <fstream>

using namespace geostudio::native;

auto Demo_Serialization = [](Application& app)
{
    if (ImGui::Button("Serialize MapNode to stdout (JSON)"))
    {
        auto map_json = app.mapNode->to_json();
        auto serialized = geostudio::native::json_pretty(map_json);

        std::ofstream outfile("out.json");
        outfile << serialized << std::endl;
        outfile.close();

        std::cout << serialized << std::endl;
    }
};
