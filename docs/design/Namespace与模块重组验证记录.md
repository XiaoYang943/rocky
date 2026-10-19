# Namespace 与模块重组验证记录

对应 [NativeSDK-002](任务/002-Namespace与模块重组.md) v2／第 1 轮。当前阶段为待人工验收。本轮自动检查不代替用户对画面、资源管理器和交互的验收。

## 交付身份

- SDK：GeoStudioNative 0.2.0，Windows x64 MSVC Release／MD。
- 安装目录：`sdk/GeoStudio-Native/out/install-modules`。
- build-manifest SHA-256：`38a32928263aba71cd87645d54160e2e0b89ae73f47cdf5c0336cf4650a85dc8`。
- 桌面程序：`demo/native-gis-demo/out/desktop-native-modules/native-gis-demo.exe`。
- EXE SHA-256：`5a70b8e671628002fff0819ad2746a37841f2497f4882ec87809135e43dbcfe9`；对应 `native/build-info.json` 内 SDK identity 必须匹配上述 manifest。
- 前一轮 `out/install` 和 `out/desktop-native-sdk` 保留；0.1 与 0.2 的 headers、libraries、DLL 不可混用。

## 实施与保护

真实 namespace 已调整：Rocky 为 `geostudio::native`，vsgXchange 为 `geostudio::native::io`，vsgCs 与 TilesetAdapter 为 `geostudio::native::tiles3d`。不是 namespace alias。源码分别进入 `src/globe`、`src/io`、`src/tiles3d`；公共 headers、private runtime headers、resources、examples、tests、tools 分开。

迁移清单记录 568 项文件移动、56 个明确文件删除及 79 个空目录移除。删除对象为被新模块构建取代的旧入口、未使用的 CI／依赖编排等，详细理由和原 hash 见 [module-migration.json](../../provenance/module-migration.json)。每个固定 upstream 文件均在 source map 登记；完整 patch 通过 reverse check。

与实施前快照核对：29 个既有技术文档／用户设计文件保持原 bytes，包括 `vector-overlay-picking.md`。无无法解释的原文件缺失。外部依赖锁、Git 历史、原始验收数据、许可证及旧制品保留。仅改变 Demo 的消费接口和资源定位，没有新增示例或数据功能。

## 已执行检查

| 检查 | 结果与范围 | 本地证据 |
| --- | --- | --- |
| source map／patch／owned files | 3 个来源身份与完整 patch 核验通过 | `out/logs/modules-build-install.log` |
| SDK build／install | Globe、IO、Tiles3D 编译、链接及安装通过 | `out/logs/modules-build-install.log` |
| 独立 installed consumer | C++17，公共 headers、target、实际链接／执行通过；GLM 隔离、新类型名和旧 I/O 类型字符串读取检查通过 | `out/logs/modules-consumer-r1.log` |
| Globe 完整套件 | 97 cases 中 96 通过、1 失败；5753 assertions 中 5737 通过、16 失败 | `out/logs/modules-build-r8.log` |
| Demo native build | 编译通过，生成 13 个样本 | Demo `out/modules-build-r1.log` |
| 原有格式／生命周期 smoke | 13／13 通过 | Demo `out/verification/native-modules-smoke.json` |
| packaged IPC | 1／1 通过，覆盖 Unicode path、rollback、viewport、EOF shutdown | Demo `out/modules-ipc-r1.log` |
| 两份实际 3D Tiles | 两份数据 selection、camera、visibility、rollback、reload 均通过；Unicode B3DM、异步错误恢复通过 | Demo `out/verification/vsgcs-streaming/run-4/result.json` |
| React／Rust 桌面构建 | TypeScript、Vite、Tauri Release 编译与打包成功 | Demo `out/modules-desktop-r1.log` |

3D Tiles 首次稳定检查的 renderableTiles：1.0 为 48，1.1 为 48。此计数说明自动回归达到 renderable 状态，不代表本轮人工检查过视觉质量。

## 失败、修复与未验证边界

1. 完整 Globe 套件未通过：唯一剩余失败为 SRS 的 Vertical Datum section；测试需要 `us_nga_egm96_15.tif`，本机 PROJ data 未提供。保留原断言和非零退出码，没有替换该 grid 或修改共享 GDAL SDK。是否补齐 grid 属于后续环境任务。
2. HTTPS case 曾因外部请求失败，最终重跑通过；不是离线测试。Shader 测试曾引用旧源码路径，已修正到 `resources/globe`，对应 cases 最终通过。
3. 编译期间修复了新布局引起的 header 查找、Windows Math.h 冲突、generated ShaderDefines 路径，以及 umbrella header 对禁用 MBTiles／Azure 的 feature guards。三个库最终可编译，未开启这些禁用功能。
4. 保留的 Qt、worldviewer、gltfviewer、vsgconv、Slughorn 可选示例／fixture 没有作为本轮交付构建；其源码和旧上游构建参考用于以后接入，不能称为已经集成。没有测试 Linux、macOS、Debug 或全新机器 bootstrap。
5. 0.2 是 C++ ABI 迁移，消费方需重编。I/O 仅保留旧 Reader 类型字符串的读取兼容，不保证任意历史自定义 VSG snapshot 的兼容性。
6. 未做 UI 自动化、性能优化、大数据 benchmark、MBTiles 新实现、提交或推送。

## 差异与规约核对

起始快照、当前差异和检查报告位于工作区 `.docs-tmp/native-sdk-r2`。规约检查覆盖 1215 个文本路径；`tests/globe/tests.cpp` 的跨文件长度增量超过工具矩阵容量，另存 `tests-diff.txt` 逐项核对，仅 include prefix、namespace 和对应 pool name 替换，没有删除断言。二进制资源迁移由 source map 原 hash／当前 hash 核验。知识 impact 分 5 批检查全部 1244 项变更路径，登记依赖命中 0，未发现登记错误；不覆盖未登记的隐式依赖。

## 手动验收

按 [安装与使用](安装构建与使用.md) 打开新 EXE；保留其旁完整 `native/` 目录。检查启动、资源管理器选择已有数据、三维球显示、图层开关及关闭／重启。建议复用以下已提供的数据：

- `D:/data/3d/osgb/DNHDL/3DTILES/3dtiles10/tileset.json`。
- `D:/data/3d/osgb/DNHDL/3DTILES/geostudio-v13/tileset.json`。
- 之前已通过的小体量 vector、XYZ、Terrain-RGB、imagery TIFF、DEM TIFF 和 OBJ。

人工结论：待用户验证。源码、包或依赖身份变化后，重新评估该结论适用性。
