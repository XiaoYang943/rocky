# GeoStudio Native SDK

GeoStudio desktop 的原生 GIS SDK，当前 API 版本 **0.3.0**、ABI 标识 **62**。源码来自固定的 Rocky、vsgXchange、vsgCs，按能力归类；主体 namespace 为 `geostudio::native`，Reader 和 Tiles runtime 分别沿用子 namespace `io`、`tiles3d`。VSG、Cesium Native、GDAL、PROJ 等外部依赖保持固定版本与原 namespace。

当前实施范围、授权和验证见 [NativeSDK-004](docs/design/SDK架构与迁移.md)。旧任务 001／002 的历史验收不自动覆盖本版本。

## 能力目录

| 目录 | CMake target | 职责 |
| --- | --- | --- |
| [src/core](src/core/README.md) | GeoStudio::Core | 基础类型、SRS、空间计算、TileKey、异步与资源协议 |
| [src/data](src/data/README.md) | GeoStudio::Data | Feature、Geometry、Image、Heightfield 的内存模型 |
| [src/scene](src/scene/README.md) | GeoStudio::Scene | Map、Layer、Entity、Viewpoint 的场景状态 |
| [src/drivers](src/drivers/README.md) | GeoStudio::Drivers | GDAL、TMS 等数据访问；可选来源独立保留 |
| [src/streaming](src/streaming/README.md) | GeoStudio::Streaming | Terrain Tile Model 组装与高程采样 |
| [src/render](src/render/README.md) | GeoStudio::Render、GeoStudio::Import | VSG 场景转换、Terrain、Entity、模型与 TileSet 渲染 |
| [src/runtime](src/runtime/README.md) | GeoStudio::Runtime、GeoStudio::RuntimeTiles | Application 装配、Viewer 生命周期、TilesetAdapter |
| 组合入口 | GeoStudio::Native | 完整 SDK |

头文件与实现同目录；公开 header 由 [显式清单](cmake/capability-headers.json) 生成构建和安装视图，源码只有一份。SDK 不依赖 ImGui；现有 Demo 需要的 UI 代码位于独立 [宿主适配](../../demo/native-gis-demo/native/ui/README.md)。Qt 不是必需项。

## 构建与验证

```powershell
Set-Location D:/code/xiaoyang943/Programming-Lab/GeoStudio/sdk/GeoStudio-Native
$env:GEOSTUDIO_NATIVE_DEPS = (Resolve-Path ../../demo/native-gis-demo/.deps).Path
./scripts/build.ps1 -Jobs 6 -WithTests
./scripts/verify-package.ps1
```

安装到 `out/install-capabilities`，构建目录 `.build/capabilities-r1`。使用 `find_package(GeoStudioNative 0.3 CONFIG REQUIRED)`。SDK 和 Demo 仍分别构建，禁止混用旧安装包。完整环境、编译和启动说明见 [安装构建与使用](docs/design/安装构建与使用.md)。

## 维护入口

- [架构与迁移](docs/design/SDK架构与迁移.md)：依赖方向、ImGui 边界、宏和 API 变化。
- [测试说明](tests/README.md)：CPU 测试、小型 fixture、环境用例及验证范围。
- [来源与完整差异](provenance/README.md)、[维护约定](GEOSTUDIO.md)：原件、映射与许可证。
- [发行目录与清理清单](docs/design/发行目录与清理清单.md)：runtime 资源、来源证明和手动清理边界。
- [原技术文档](docs/技术文档/README.md)：历史路径用迁移映射定位。

本轮不运行图形软件、不作画面验收；GeoJSON 渲染问题、Demo 布局和统一构建继续暂缓。历史 examples／tools 已按本轮明确授权清理；原始来源与许可证保留。未暂存、提交或推送 Git。
