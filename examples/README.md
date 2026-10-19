# 保留示例与源码索引

本轮只整理 namespace 和模块，没有把以下示例接入 native-gis-demo。原来源 commit 与每个原路径／新路径见 [迁移映射](../provenance/module-migration.json)。旧库名称仍用于部分示例文件名，方便查找原始实现。

| 目录 | 来源及用途 | 构建边界 |
| --- | --- | --- |
| [globe/rocky_demo](globe/rocky_demo) | Rocky Demo_*：Geometry、Label、Picking、Camera、Elevation query、Terrain、Overlay 等 | SDK 的 GEOSTUDIO_BUILD_EXAMPLES 可选；本轮不运行或验收新 UI 功能 |
| [globe/rocky_simple](globe/rocky_simple) | 最小 Application | 与 SDK 同一 namespace |
| [globe/rocky_demo_qt](globe/rocky_demo_qt) | Qt 原示例 | 源码参考，当前构建不启用 Qt |
| [globe/rocky_sandbox](globe/rocky_sandbox) | 渲染实验 | 源码参考，当前不启用 |
| [tiles3d/worldviewer](tiles3d/worldviewer) | vsgCs 场景、Tileset 与 Overlay | 源码参考；不能直接代替现有宿主 Viewer／生命周期 |
| [tiles3d/gltfviewer](tiles3d/gltfviewer) | glTF 渲染 | 源码参考；private runtime 未公开安装 |
| [tiles3d/config](tiles3d/config) | World／Raster Overlay 配置样例 | 在线样例需要另行准备服务与凭据 |
| [I/O 转换源码](../tools/io/vsgconv) | vsgXchange vsgconv | 工具参考，本轮未构建 |
| [package-consumer](package-consumer) | 当前 SDK 独立安装包验证 | scripts/verify-package.ps1 执行 |

来源文本原件在 provenance 下，技术能力并未因为改名而丢失。下一轮按具体功能接入，复用 scene／render logic，处理 Entity、Node、callback 的关闭与重置，不直接搬入第二套 Viewer。
