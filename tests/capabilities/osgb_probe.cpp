#include "OSGBStats.h"
#include <geostudio/native/render/import/osgb/OSGB.h>
#include <iostream>
#include <filesystem>
#include <chrono>

// 探测一个入口和每层首个后代，深度上限为 4；显式传入缓存目录才生成派生页，不创建窗口。
int main(int argc, char** argv)
{
    if (argc < 2 || argc > 4) { std::cerr << "Usage: geostudio-osgb-probe <file-or-dataset> [depth:0..4] [cache-directory]\n"; return 2; }
    try
    {
        const int depth = argc >= 3 ? std::stoi(argv[2]) : 0;
        if (depth < 0 || depth > 4) return 2;
        auto reader = geostudio::native::io::OSGB::create();
        if (argc == 4) { geostudio::native::io::OSGB::Settings settings; settings.cacheDirectory = argv[3]; reader->configure(settings); reader->enablePageCache(true); }
        const auto begin = std::chrono::steady_clock::now();
        auto node = reader->read(argv[1]).cast<vsg::Node>();
        std::cout << "entryCpuMs=" << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count() << '\n';
        for (int level = 0; level <= depth; ++level)
        {
            if (!node) { std::cerr << reader->error() << '\n'; return 1; }
            OSGBStats stats; node->accept(stats);
            vsg::ComputeBounds bounds; node->accept(bounds);
            if (!bounds.bounds.valid()) return 1;
            std::cout << "level=" << level << " draws=" << stats.draws << " textures=" << stats.textures << " pages=" << stats.pages.size() << '\n';
            if (level == 0)
            {
                for (const auto* key : {"longitude", "latitude", "altitude", "range"})
                { double value = 0; if (node->getValue(std::string("osgb.") + key, value)) std::cout << key << '=' << value << '\n'; }
            }
            if (stats.pages.empty() || level == depth) break;
            auto page = stats.pages.front();
            // 输出源选择参数与剔除球，便于只读核验真实瓦片；不推断屏幕上的实际清晰度。
            bool sourceLOD = false;
            page->getValue("osgb.sourceLOD", sourceLOD);
            std::cout << "sourceLOD=" << sourceLOD << " cullRadius=" << page->bound.r << '\n';
            for (const auto* key : {"osgb.pixelSplit", "osgb.distanceSplit"})
            {
                double value = 0.0;
                if (page->getValue(key, value)) std::cout << key << '=' << value << '\n';
            }
            for (const auto* key : {"osgb.lodSphere", "osgb.distanceSphere"})
            {
                vsg::dvec4 value;
                if (page->getValue(key, value))
                    std::cout << key << '=' << value.x << ',' << value.y << ',' << value.z << ',' << value.w << '\n';
            }
            std::cout << "next=" << page->filename.string() << '\n';
            node = vsg::read_cast<vsg::Node>(page->filename, page->options);
        }
        const auto performance = reader->performance();
        std::cout << "decodeMs=" << performance.decodeMilliseconds << " prepareMs=" << performance.prepareMilliseconds
            << " convertMs=" << performance.convertMilliseconds << " shaderCpuMs=" << performance.shaderMilliseconds
            << " pipelineHits=" << performance.pipelineHits << " indexedRoots=" << performance.indexedRoots
            << " cacheHits=" << performance.cacheHits << " cacheMisses=" << performance.cacheMisses << " cacheWrites=" << performance.cacheWrites
            << " cacheLoadMs=" << performance.cacheLoadMilliseconds << " cacheStoreMs=" << performance.cacheStoreMilliseconds
            << " cacheDiagnostic=" << performance.cacheDiagnostic << '\n';
        reader->close();
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
