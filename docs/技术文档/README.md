# Rocky 技术文档

## 目标与当前能力

本目录面向初次接触 OSG、osgEarth、VSG 和原生三维 GIS 的 GeoStudio 开发者，按“基础概念 → 架构 → 公开接口 → 内部实现 → 能力边界 → 验证方法”组织源码导读。

Rocky 导读已按[梳理方案 v1](梳理方案.md)完成全部七篇 P0 源码导读，并在第 3 轮补充术语表、统一英文用词，覆盖背景知识、架构、坐标、公共 Tile 机制及三条数据路线。该交付为文档和静态源码分析，C++ 片段、GPU 场景及性能用例尚未运行；原 Rocky 导读的内容身份、自检及人工验收状态以[主任务](任务/001-Rocky架构与技术实现梳理.md)第 3 轮为准，第 2 轮结果保留为历史。

独立的[渲染基础库调研](调研分析/README.md)已完成方案 v1 的全部 P0：VSG 核心运行链、vsgXchange 数据读取与 3D Tiles 链，以及 OSG/osgEarth 职责概览。其深入程度与后续 P1/P2 分开标识；该次导航与术语补充的原内容身份、自检及人工验收状态见[主任务 002](任务/002-渲染基础库架构调研.md)，不改写任务 001 的历史轮次。

新增 [vsgCs 架构与数据链路](调研分析/vsgCs/README.md)，覆盖 Cesium Native/VSG 的连接、3D Tiles、Quantized Mesh、PNTS、Overlay、UI 和构建边界。本次相关导航及术语的当前文件身份由[主任务 003](任务/003-vsgCs架构与数据链路调研.md)登记；旧任务与原报告保留。

源码来源为 [XiaoYang943/rocky](https://github.com/XiaoYang943/rocky)，当前 `main` 基线为 `a4d8b141ec478e8440526928b344c6eb93810631`，CMake 声明 Rocky 1.1.0、ABI 60。克隆保留完整可达历史，未采用浅克隆。本目录按照用户明确指定的位置维护；上游 [AGENTS](../../AGENTS.md) 中的 `docs/design` 约定不改变本次文档落点。

## 文档与任务入口

| 入口 | 内容 |
| --- | --- |
| [术语](术语.md) | 中英文对照、源码标识与易混淆边界；底层术语优先使用英文 |
| [基础知识](基础知识/README.md) | OSG/osgEarth/VSG/Rocky、Scene Graph、CPU/GPU、坐标、Tile 和 ECS |
| [总体架构](总体架构/README.md) | Application、Map/MapNode、上下文、数据链路与 Frame Loop |
| [坐标系统](坐标系统/README.md) | SRS/PROJ、Axis Order、经纬高/ECEF/ENU、范围和线程上下文 |
| [Layer 与 Tile](图层与瓦片/README.md) | Layer 生命周期、Profile/TileKey、范围、Level、Mosaicing 和 Cache |
| [栅格](栅格/README.md) | TMSImageLayer、GDALImageLayer、影像读取与 Compositing |
| [三维地形](三维地形/README.md) | TMSElevationLayer、GDALElevationLayer、高程编码、Mesh 与 Paging |
| [矢量](矢量/README.md) | GDALFeatureSource、FeatureBuilder、ECS、NodePager 和 MVT 现成路线 |
| [基础库调研](调研分析/README.md) | OSG、osgEarth、VSG、vsgXchange、vsgCs 的职责、运行链与源码边界 |
| [VSG 架构](调研分析/VSG/README.md) | Scene Graph、Compile、Frame Loop、I/O 与 DatabasePager |
| [vsgXchange 架构](调研分析/vsgXchange/README.md) | ReaderWriter、Options、模型/图像、3D Tiles 与明确缺口 |
| [vsgCs 架构](调研分析/vsgCs/README.md) | Native Tile Selection、Renderer Adapter、数据能力与接入优先级 |
| [梳理方案](梳理方案.md) | 模块范围、P0/P1/P2、重点问题、交付深度和阅读顺序 |
| [任务索引](任务/README.md) | Rocky 导读与基础库调研的独立主任务 |
| [主任务 001](任务/001-Rocky架构与技术实现梳理.md) | 获取仓库、授权、来源身份、检查和后续轮次 |
| [主任务 002](任务/002-渲染基础库架构调研.md) | 基础库调研 P0、术语补充、证据、自检及后续范围 |
| [主任务 003](任务/003-vsgCs架构与数据链路调研.md) | vsgCs 独立调研、固定来源、能力边界和本轮自检 |

先查阅术语，再按基础知识、总体架构、坐标系统、Layer 与 Tile 建立基础，最后阅读栅格、三维地形和矢量。`数据访问与任务调度`、`场景与交互`、`工程集成`、`能力矩阵与验证` 属于 P1，`高级专题` 属于 P2；这些后续章节尚未展开，已有 P0 中只解释理解当前路线必需的部分。

## 本版阅读时需要明确的边界

- `SRS::WGS84` 的别名在固定提交中映射到 EPSG:4979；Axis Order 与垂直语义须按实际实现理解。
- `GDALFeatureSource` 读取 Feature，FeatureBuilder 构建 Geometry，ECS 绘制；已有 NodePager/MVT 示例可复用，不应从 GeoStudio Demo 的单 Tile 范围推导底层缺少 Paging 设施。
- 地形输入 RGB、CPU 浮点高度和 GPU 内部 R16 是不同表示；当前地形工厂只取首个打开的高程层，不自动融合多个 DEM。
- 公开接口、构建启用和本机真实数据验证分开记录；旧 README 与当前头文件不一致的地方在对应章节说明。

## 代码与契约入口

| 主题 | 首要源码入口 | 说明 |
| --- | --- | --- |
| 应用与渲染连接 | [Application.h](../../src/rocky/vsg/Application.h)、[MapNode.h](../../src/rocky/vsg/MapNode.h)、[VSGContext.h](../../src/rocky/vsg/VSGContext.h) | 应用组织、地图场景 Node 和 VSG 上下文 |
| 地图数据模型 | [Map.h](../../src/rocky/Map.h)、[Layer.h](../../src/rocky/Layer.h)、[TileLayer.h](../../src/rocky/TileLayer.h) | Layer 与 Tile 请求基础 |
| 坐标和 Tile | [SRS.h](../../src/rocky/SRS.h)、[Profile.h](../../src/rocky/Profile.h)、[TileKey.h](../../src/rocky/TileKey.h) | SRS、Profile 和 Tile 寻址 |
| 影像 | [TMSImageLayer.h](../../src/rocky/TMSImageLayer.h)、[GDALImageLayer.h](../../src/rocky/GDALImageLayer.h) | 切片影像和 GDAL 数据源 |
| 高程 | [TMSElevationLayer.h](../../src/rocky/TMSElevationLayer.h)、[GDALElevationLayer.h](../../src/rocky/GDALElevationLayer.h) | 高程 Tile 和原始高程数据源 |
| 矢量 | [GDALFeatureSource.h](../../src/rocky/GDALFeatureSource.h)、[FeatureBuilder.h](../../src/rocky/vsg/ecs/FeatureBuilder.h) | Feature 读取与可绘制 Geometry 生成是不同环节 |
| 地形 Paging | [TerrainTilePager.h](../../src/rocky/vsg/terrain/TerrainTilePager.h) | 地形 Node 的数据加载与合并调度 |
| 构建开关 | [CMakeLists.txt](../../CMakeLists.txt) | 公开接口是否可用还取决于可选依赖和构建结果 |

## 关键约束

- 源码结论对应上述固定提交，正文用文件、稳定符号和必要行段定位；外部资料仅作概念或规范补充。
- 区分源码观察、官方说明、设计建议、实际验证和待验证结果；接口声明存在不能代替端到端可用。
- 当前阶段优先利用现成公开接口。MBTiles、MVT、3D Tiles、点云等逐项核查读取、调度、渲染责任，缺失能力单列，不默认进入自研。
- 本地源码副本用于分析；现有 Native GIS Demo 仍消费其锁定的 `.deps/src` 源码副本，尚未切换到本目录。
- 文档采用原理图和调用链解释实现。OSG/osgEarth 背景用于帮助理解，不将它们当作 Rocky 的运行依赖。

## 验证入口

任务 001 的术语复核见其第 3 轮，四库调研复核见任务 002，vsgCs 及本次相关文件身份见任务 003；各任务按各自内容身份判断。各章的运行用例和 C++ 片段均明确注明尚未执行，不以源码阅读代替独立构建或 GPU 验收。Rocky 导读交付标准见[原方案](梳理方案.md#验证与交付标准)，基础库原交付标准见[调研方案](调研分析/梳理方案.md#证据验证与关卡)，vsgCs 标准见其主任务。

## 上下文恢复

先读本页、方案和主任务，再读工作区公共规范及仓库 AGENTS。源码行为改变时复核关联正文；原生宿主和数据加载验证的历史事实仍维护在 `demo/native-gis-demo`，本目录只引用其实际版本与证据。
