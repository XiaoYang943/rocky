#include <geostudio/native/runtime/engine/TilesetAdapter.h>
#include <geostudio/native/render/tileset/cesium/RuntimeEnvironment.h>
#include <geostudio/native/render/geospatial/cesium/WorldNode.h>
#include <geostudio/native/render/tileset/cesium/TilesetNode.h>
#include <geostudio/native/core/async/cesium/OpThreadTaskProcessor.h>
#include <geostudio/native/render/model/cesium/pbr.h>
#include <Cesium3DTilesSelection/BoundingVolume.h>
#include <Cesium3DTilesSelection/TilesetLoadFailureDetails.h>
#include <CesiumGeospatial/Ellipsoid.h>
#include <CesiumUtility/Math.h>
#include <vsg/nodes/Switch.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>

namespace geostudio::native::tiles3d
{
using Json = nlohmann::json;
namespace
{
struct LoadState
{
    std::mutex mutex;
    std::string error;
    bool destroyed = false;
    bool rootReady = false;
};

// Native owns selection and streaming; the host supplies its own camera directly.
// This avoids the upstream automatic View registration retaining a Tileset path.
class ManagedTileset : public vsg::Inherit<geostudio::native::tiles3d::TilesetNode, ManagedTileset>
{
public:
    // Reuse the process runtime and upstream content loader.
    ManagedTileset(const geostudio::native::tiles3d::TilesetSource& source, const Cesium3DTilesSelection::TilesetOptions& options) :
        Inherit(geostudio::native::tiles3d::RuntimeEnvironment::get()->features, source, options, geostudio::native::tiles3d::RuntimeEnvironment::get()->options) {}

    // Update selection and visibility uniforms on the render thread.
    void update(const vsg::Camera& camera, float delta)
    {
        const auto viewport = camera.viewportState->getViewport();
        std::vector<Cesium3DTilesSelection::ViewState> views;
        views.emplace_back(geostudio::native::tiles3d::vsg2glm(camera.viewMatrix->transform()),
            geostudio::native::tiles3d::vsg2glm(camera.projectionMatrix->transform()), glm::dvec2(viewport.width, viewport.height));
        _viewUpdateResult = &_tileset->updateViewGroup(_tileset->getDefaultViewGroup(), views, delta);
        for (const auto& tile : _viewUpdateResult->tilesToRenderThisFrame)
        {
            if (const auto* content = tile->getContent().getRenderContent())
            {
                const auto* resources = static_cast<const geostudio::native::tiles3d::RenderResources*>(content->getRenderResources());
                if (resources && resources->model)
                {
                    if (auto data = geostudio::native::tiles3d::CesiumGltfBuilder::getTileData(resources->model))
                    {
                        auto [previous, fading] = geostudio::native::tiles3d::pbr::getFadeValue(data);
                        if (previous != 1.0f || fading) { geostudio::native::tiles3d::pbr::setFadeValue(data, 1.0f, false); data->dirty(); }
                    }
                }
            }
        }
        _tileset->loadTiles();
    }

    // Borrow the current selection until the next update or shutdown.
    const Cesium3DTilesSelection::ViewUpdateResult* result() const { return _viewUpdateResult; }
    // Clear the borrowed selection before asynchronous shutdown.
    void stopRendering() { _viewUpdateResult = nullptr; }
};

// Validate local roots and encode UTF-8 paths; HTTP URLs pass through unchanged.
std::string sourceUrl(const std::string& uri)
{
    if (uri.starts_with("http://") || uri.starts_with("https://")) return uri;
    // The Explorer entry supplies a filesystem path, not an already escaped URL.
    auto path = std::filesystem::absolute(std::filesystem::u8path(uri)).lexically_normal();
    if (!std::filesystem::is_regular_file(path)) throw std::runtime_error("Tileset file does not exist: " + uri);
    std::ifstream input(path);
    Json document;
    input >> document;
    if (!document.contains("root") || !document.contains("asset")) throw std::runtime_error("Expected a 3D Tiles tileset.json");
    auto utf8 = path.generic_u8string();
    std::string bytes(reinterpret_cast<const char*>(utf8.data()), utf8.size());
    std::string encoded;
    constexpr char hex[] = "0123456789ABCDEF";
    for (const unsigned char c : bytes)
    {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/' || c == ':' || c == '-' || c == '_' || c == '.' || c == '~') encoded += char(c);
        else { encoded += '%'; encoded += hex[c >> 4]; encoded += hex[c & 15]; }
    }
    return bytes.starts_with("//") ? "file:" + encoded : "file:///" + encoded;
}
}

vsg::ref_ptr<vsg::Window> createTilesetWindow(vsg::ref_ptr<vsg::WindowTraits> traits)
{
    geostudio::native::tiles3d::startup();
    geostudio::native::tiles3d::setMainThread();
    auto env = geostudio::native::tiles3d::RuntimeEnvironment::get();
    int argc = 1;
    char name[] = "geostudio-native";
    char* argv[] = {name, nullptr};
    vsg::CommandLine arguments(&argc, argv);
    env->initializeOptions(arguments);
    env->options->paths.emplace_back("vsgCs");
    env->initializeTraits(arguments, traits);
    env->enableLodTransitionPeriod = false;
    env->enableProjNetwork = false;
    auto window = env->openWindow("GeoStudio Native GIS", traits, env->options);
    if (!window) throw std::runtime_error("Unable to create the shared Vulkan window");
    return window;
}

void setTilesetViewer(vsg::ref_ptr<vsg::Viewer> viewer) { geostudio::native::tiles3d::RuntimeEnvironment::get()->setViewer(viewer); }

void pumpTilesetTasks()
{
    auto env = geostudio::native::tiles3d::RuntimeEnvironment::get();
    env->getAssetAccessor()->tick();
    geostudio::native::tiles3d::getAsyncSystem().dispatchMainThreadTasks();
}

void shutdownTilesetRuntime()
{
    geostudio::native::tiles3d::shutdown();
    auto env = geostudio::native::tiles3d::RuntimeEnvironment::get();
    // Release GPU ownership before DLL detach, while Vulkan and the host are alive.
    // The caller has already retired all tilesets and waited for the device.
    env->setViewer({});
    auto externals = env->getTilesetExternals();
    externals->pPrepareRendererResources.reset();
    externals->pAssetAccessor.reset();
    env->genv = {};
    env->options = {};
    env->traits = {};
}

struct TilesetAdapter::Impl
{
    vsg::ref_ptr<vsg::Viewer> viewer;
    vsg::ref_ptr<vsg::View> view;
    vsg::ref_ptr<geostudio::native::tiles3d::WorldNode> world;
    vsg::ref_ptr<ManagedTileset> node;
    vsg::ref_ptr<vsg::Switch> display;
    std::shared_ptr<LoadState> load = std::make_shared<LoadState>();
    bool visible = true, closed = false;
    std::chrono::steady_clock::time_point last = std::chrono::steady_clock::now();
    Json stats = {{"backend", "vsgCs"}, {"status", "loading"}, {"renderableTiles", 0}};
    // Retain the host viewer and view for the adapter lifetime.
    Impl(vsg::ref_ptr<vsg::Viewer> inViewer, vsg::ref_ptr<vsg::View> inView) : viewer(inViewer), view(inView) {}
};

TilesetAdapter::TilesetAdapter(vsg::ref_ptr<vsg::Viewer> viewer, vsg::ref_ptr<vsg::View> view,
    const Json& config, const Compile& compile) : _impl(std::make_unique<Impl>(viewer, view))
{
    auto& p = *_impl;
    geostudio::native::tiles3d::TilesetSource source;
    source.url = sourceUrl(config.at("uri").get<std::string>());
    Cesium3DTilesSelection::TilesetOptions options;
    options.maximumScreenSpaceError = config.value("maximumScreenSpaceError", 16.0);
    options.maximumSimultaneousTileLoads = 8;
    options.maximumCachedBytes = 512 * 1024 * 1024;
    options.forbidHoles = true;
    options.loadErrorCallback = [state = p.load](const Cesium3DTilesSelection::TilesetLoadFailureDetails& failure) {
        std::lock_guard lock(state->mutex);
        state->error = failure.message;
    };
    p.world = geostudio::native::tiles3d::WorldNode::create();
    // Bind the upstream World/View descriptor sets without installing its camera operations.
    p.world->initialize(p.viewer);
    p.display = vsg::Switch::create();
    p.display->addChild(vsg::MASK_ALL, p.world);
    // Compile with this View's state and the host render pass.
    auto compiled = compile(p.display);
    if (!compiled) throw std::runtime_error("Tileset world GPU compilation failed: " + compiled.message);
    p.node = ManagedTileset::create(source, options);
    p.node->getTileset()->getRootTileAvailableEvent().thenInMainThread([state = p.load]() {
        state->rootReady = true;
    }).catchInMainThread([state = p.load](std::exception&& error) {
        std::lock_guard lock(state->mutex);
        state->rootReady = true;
        state->error = error.what();
    });
    p.world->tilesetNodes().push_back(p.node);
    p.view->addChild(p.display);
}

TilesetAdapter::~TilesetAdapter() = default;

void TilesetAdapter::update()
{
    auto& p = *_impl;
    if (p.closed) return;
    const auto now = std::chrono::steady_clock::now();
    if (p.visible) p.node->update(*p.view->camera, std::min(0.1f, std::chrono::duration<float>(now - p.last).count()));
    p.last = now;
    auto* tileset = p.node->getTileset();
    p.stats["loadedTiles"] = tileset->getNumberOfTilesLoaded();
    p.stats["dataBytes"] = tileset->getTotalDataBytes();
    if (const auto* result = p.node->result())
    {
        p.stats["selectedTiles"] = result->tilesToRenderThisFrame.size();
        p.stats["maxDepthVisited"] = result->maxDepthVisited;
        p.stats["pendingWorkerTiles"] = result->workerThreadTileLoadQueueLength;
        p.stats["pendingMainTiles"] = result->mainThreadTileLoadQueueLength;
        Json selected = Json::array();
        size_t renderable = 0;
        for (const auto& tile : result->tilesToRenderThisFrame)
        {
            const auto* content = tile->getContent().getRenderContent();
            const auto* resources = content ? static_cast<const geostudio::native::tiles3d::RenderResources*>(content->getRenderResources()) : nullptr;
            if (!resources || !resources->model) continue;
            ++renderable;
            if (selected.size() < 8)
                if (const auto* id = std::get_if<std::string>(&tile->getTileID())) selected.push_back(*id);
        }
        p.stats["renderableTiles"] = renderable;
        p.stats["selectedContents"] = std::move(selected);
    }
    std::lock_guard lock(p.load->mutex);
    if (p.load->rootReady && !tileset->getRootTile() && p.load->error.empty())
        p.load->error = "Tileset root could not be created; check the boundingVolume and engine log";
    p.stats["error"] = p.load->error;
    p.stats["status"] = !p.load->error.empty() ? "error" : p.stats["renderableTiles"].get<size_t>() > 0 ? "renderable" : "loading";
}

void TilesetAdapter::setVisible(bool visible)
{
    _impl->visible = visible;
    _impl->display->children.front().mask = visible ? vsg::MASK_ALL : vsg::MASK_OFF;
}

void TilesetAdapter::close()
{
    auto& p = *_impl;
    if (p.closed) return;
    p.closed = true;
    auto& children = p.view->children;
    children.erase(std::remove(children.begin(), children.end(), p.display), children.end());
    p.node->stopRendering();
    p.node->getTileset()->getAsyncDestructionCompleteEvent().thenInMainThread([state = p.load]() { state->destroyed = true; });
    p.node->shutdown();
}

bool TilesetAdapter::retired() const { return _impl->closed && _impl->load->destroyed; }
Json TilesetAdapter::evidence() const { return _impl->stats; }

std::optional<Json> TilesetAdapter::viewpoint() const
{
    const auto& p = *_impl;
    if (p.closed) return {};
    const auto* root = p.node->getTileset()->getRootTile();
    if (!root) return {};
    const auto sphere = Cesium3DTilesSelection::getOrientedBoundingBoxFromBoundingVolume(root->getBoundingVolume()).toSphere();
    const auto carto = CesiumGeospatial::Ellipsoid::WGS84.cartesianToCartographic(sphere.getCenter());
    if (!carto) return {};
    return Json{{"longitude", CesiumUtility::Math::radiansToDegrees(carto->longitude)},
        {"latitude", CesiumUtility::Math::radiansToDegrees(carto->latitude)}, {"altitude", carto->height},
        {"range", std::max(50.0, sphere.getRadius() * 4.0)}};
}
}
