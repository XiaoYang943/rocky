# GeoStudio Native SDK

GeoStudio desktop 的原生 GIS SDK，当前 API 版本 0.2.0。源码由固定的 Rocky、vsgXchange、vsgCs 演进，使用真实的 `geostudio::native` namespace。VSG、Cesium Native、GDAL、PROJ、GLM 等外部依赖保持固定版本与原 namespace。

当前实施与验证见 [NativeSDK-002](docs/design/任务/002-Namespace与模块重组.md)。[NativeSDK-001](docs/design/任务/001-NativeSDK整合P0.md) 保留上一版整合和验收事实。本轮不增加 Demo 功能，已有 Demo 只适配新 SDK。

## 模块

| CMake target | namespace | 源码／公开 header | 职责 |
| --- | --- | --- | --- |
| GeoStudio::Globe | geostudio::native | [src/globe](src/globe)、[include](include/geostudio/native/globe) | Map、SRS、Terrain、Feature、ECS、Application |
| GeoStudio::IO | geostudio::native::io | [src/io](src/io)、[include](include/geostudio/native/io) | Reader／Writer；shared library |
| GeoStudio::Tiles3D | geostudio::native::tiles3d | [src/tiles3d](src/tiles3d)、[include](include/geostudio/native/tiles3d) | TilesetAdapter；private Streaming runtime 与 Cesium Native |
| GeoStudio::Native | 组合入口 | [CMake 装配](cmake/GeoStudioNative.cmake) | 同时消费上述模块 |

Globe 采用 static linkage，共享宿主 ImGui context；Tiles3D 在 DLL 内隔离 C++20 与 GLM 编译定义，public consumer 可使用 C++17。Qt 不是必需项。底层 feature macros 和 Shader 名称保留部分来源前缀，不表示仍使用旧 C++ namespace。

## 构建与使用

详见[安装、构建与使用](docs/design/安装构建与使用.md)。当前本机复用外部依赖 Cache：

```powershell
Set-Location D:/code/xiaoyang943/Programming-Lab/GeoStudio/sdk/GeoStudio-Native
$env:GEOSTUDIO_NATIVE_DEPS = (Resolve-Path ../../demo/native-gis-demo/.deps).Path
./scripts/build.ps1 -Jobs 6 -WithTests
./scripts/verify-package.ps1
```

新 ABI 安装到 `out/install-modules`，编译目录为 `.build/modules-r1`；旧 `out/install` 保留。不得把旧 namespace 的 DLL／静态库与新 header 混用。CMake 使用 `find_package(GeoStudioNative 0.2 CONFIG REQUIRED)`。

## 文档与来源

- [模块与 API 迁移](docs/design/SDK架构与迁移.md)：目录、namespace、兼容边界。
- [来源](GEOSTUDIO.md)、[provenance](provenance/README.md)：固定原件、完整 patches、旧路径到新路径映射。
- [示例索引](examples/README.md)：保留现有示例以便继续查阅，本轮不集成新功能。
- [原技术文档](docs/技术文档/README.md)：历史源码名保留，当前路径按迁移映射定位。
- [独立安装包 consumer](examples/package-consumer/main.cpp)：新 API、类型注册、旧 Reader 类型标识兼容及 GLM 隔离检查。

输入数据、已有设计／技术文档、用户配置和原 Git 历史保留。未提交或推送 Git；远端仓库仍采用原配置。
