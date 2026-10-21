# SDK 架构与迁移

当前 GeoStudioNative 0.3.0，主任务 [NativeSDK-003](任务/003-能力模块重组与代码测试.md)。头源同目录、按能力组织，来源以 provenance 映射保留；没有保留 globe／io／tiles3d 三个顶层源码组件。

## 依赖与职责

```mermaid
graph TD
    Host[React / Tauri 宿主] --> Runtime
    Demo[Demo 的可选 ImGui adapter] --> Runtime
    Runtime --> Render
    Runtime --> RuntimeTiles[RuntimeTiles DLL]
    Render --> Streaming
    Render --> Drivers
    Render --> Import[Import DLL]
    Render --> Scene
    Streaming --> Scene
    Drivers --> Scene
    Scene --> Data
    Data --> Core
    RuntimeTiles --> CesiumAdapter[私有 Cesium adapter]
    CesiumAdapter --> Cesium[外部 Cesium Native]
    Import --> VSG
    Render --> VSG
```

`src/core` 包含 base／geo／math／async／cache／io；`src/data` 包含 vector／raster／elevation；`src/scene` 定义 Map、Layer、Entity 和 Viewpoint；`src/drivers` 按数据源分组；`src/streaming` 负责 Terrain 数据组装；`src/render` 负责 VSG 表达、分页和渲染算法；`src/runtime` 只负责装配、运行和生命周期。

VSG 的分类职责和 osgEarth 的 Drivers 边界用于指导结构，具体分层以本工程的真实 include 和链接依赖为准。此轮抽离 Terrain Picking 到 render/view，使 DisplayManager／MapManipulator 无需反向包含 Application。Core/Data 的公开链接不依赖 VSG，Core 仍因现有 Context 实现使用 GDAL、PROJ、CURL；完全独立的微型 Core package 不是本轮交付。

原 vsgXchange 中把输入转换为 VSG Object 的 Reader 放在 render/import；GIS 原始数据访问在 drivers，不把两者混为一个 data 层。原 vsgCs 的 HTTP、异步、坐标、模型和资源代码分散到对应能力的 cesium 子目录；为隔离 C++20、GLM 与 Cesium 对象布局，这些实现由一个私有 target 编译，不能链接进公开 C++17 能力库。

## 文件、公开接口和 ABI

头文件与实现放在同一职责目录。`cmake/capability-sources.cmake` 显式列出公共能力源文件；`cmake/capability-headers.json` 记录每个 header 的源位置、include 名和 public 标志。构建时生成 include 视图，不移动原文件、不维护第二份手写 header。

消费者示例：

```cpp
#include <geostudio/native/core/geo/SRS.h>
#include <geostudio/native/drivers/gdal/GDALImageLayer.h>
#include <geostudio/native/runtime/engine/Application.h>
#include <geostudio/native/runtime/engine/TilesetAdapter.h>
```

旧 include 路径不继续安装；CMake 的 Globe／IO／Tiles3D target alias 暂时映射到 Runtime／Import／RuntimeTiles，方便已有消费方按次序迁移，不表示保留旧组件。namespace 保持 `geostudio::native`、`geostudio::native::io`、`geostudio::native::tiles3d`。VSG Reader 的旧序列化类型字符串继续保留读取映射，Map／Layer JSON 和 3D Tiles 数据协议不改名。

SDK package 升至 0.3.0，ABI 标识 62；安装路径 `out/install-capabilities`。七个能力库为 static，Import／RuntimeTiles 为 DLL。默认 consumer 为 C++17，Cesium 私有实现为 C++20。不同 package、CRT 或 GLM 配置不能混用。

## ImGui 由宿主管理

SDK 的 Application、View、VSGContext、ECSNode 已移除 ImGui context、自动 GUI 安装和 Widget 绘制。纯 Label 状态仍属于 Scene；LabelSystem 的 ImGui 绘制、Widget、RenderImGui 和事件翻译放在 Demo 的 `native/ui`，仅由需要它的宿主链接。Demo 调用 `geostudio::host::installImGui`，不再调用 Application::install(renderer)。

原 vsgCs 默认禁用且无调用的 readRemoteTexture／vsgImGui::Texture 包装已移除；通用 readRemoteImage 保留。历史实现仍可从完整来源差异追溯。SDK package 不查找 imgui，bootstrap 默认也不编译它；需要旧 Demo 时显式 `-WithHostUi` 或 `-Only imgui`。

未来 Tauri＋React 可以继续使用独立进程／Native Window 的现有宿主边界；本轮没有新增 JS 绑定或跨线程渲染 API。保留 Viewer、View、Camera 的共享关系，TilesetAdapter 的 close／pump／retired／shutdown 顺序不变。

## 宏、资源和可选能力

自有活动源码、CMake、测试、示例和 GLSL 的 ROCKY_* 改为 GEOSTUDIO_NATIVE_*。公共 static API 使用 GEOSTUDIO_NATIVE_API，RuntimeTiles DLL 使用 GEOSTUDIO_NATIVE_EXPORT，避免导出宏冲突。版本比较宏补上正确括号并加编译断言。资源文件名和数据协议保留原名；外部 VSG／Cesium／第三方宏不强制重命名。

Slughorn 的实际 Adapter 和 SlugSystem 保留源码，默认不编译；其纯算法回归继续执行。MBTiles、Azure、Bing 等仍受原 feature guard 控制，不因文件归类宣称启用新格式。保留来源中的实验代码和旧示例作为参考，不把它们加入默认 SDK 构建。

## 检查与来源

[代码测试](../../tests/README.md)覆盖基础算法、现有渲染相关 CPU 逻辑、Reader、局部资源协议和小型 fixture。架构检查拒绝反向 include、UI 泄漏、私有 header 安装和旧宏。独立 package consumer 检查公开 header、类型注册、Core/Data 单独链接和 GLM 隔离。

[capability-migration.json](../../provenance/capability-migration.json)记录旧路径到新路径；每个来源的 005-capabilities.patch 是对固定原件的完整差异，旧 patches 保留历史。版权和许可内容不改。当前不做 GPU、性能、渲染回归或人工验收，不能将 CPU 通过扩展为这些结论。
