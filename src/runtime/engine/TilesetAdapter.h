#pragma once
#include <geostudio/native/runtime/engine/Export.h>
#include <vsg/all.h>
#include <nlohmann/json.hpp>
#include <functional>
#include <memory>
#include <optional>

namespace geostudio::native::tiles3d
{
// Configure both libraries before the shared Vulkan device is created.
GEOSTUDIO_NATIVE_EXPORT vsg::ref_ptr<vsg::Window> createTilesetWindow(vsg::ref_ptr<vsg::WindowTraits> traits);
// Bind the shared viewer on the render thread after window creation.
GEOSTUDIO_NATIVE_EXPORT void setTilesetViewer(vsg::ref_ptr<vsg::Viewer> viewer);
// Pump HTTP and main-thread continuations once per frame, including during retirement.
GEOSTUDIO_NATIVE_EXPORT void pumpTilesetTasks();
// Shut down the process-wide runtime after every adapter retires and GPU work finishes.
GEOSTUDIO_NATIVE_EXPORT void shutdownTilesetRuntime();

// Render-thread adapter. The host must close(), pump until retired(), then release it.
class GEOSTUDIO_NATIVE_EXPORT TilesetAdapter
{
public:
    using Compile = std::function<vsg::CompileResult(vsg::ref_ptr<vsg::Object>)>;
    // Attach to the supplied View; compile uses its render-pass context and may throw.
    TilesetAdapter(vsg::ref_ptr<vsg::Viewer> viewer, vsg::ref_ptr<vsg::View> view,
        const nlohmann::json& config, const Compile& compile);
    // Release ownership after retirement and device idle; no blocking shutdown here.
    ~TilesetAdapter();
    // Select visible content using the host camera; call before recording the frame.
    void update();
    // Toggle traversal without releasing loaded content.
    void setVisible(bool visible);
    // Detach once and begin asynchronous destruction; continue pumping afterward.
    void close();
    // True once close() and asynchronous destruction have both completed.
    bool retired() const;
    // Return a copy of streaming diagnostics, not a visual correctness guarantee.
    nlohmann::json evidence() const;
    // Return WGS84 framing from the root bound, or no value before root availability.
    std::optional<nlohmann::json> viewpoint() const;
private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};
}
