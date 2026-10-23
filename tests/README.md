# 代码侧测试与 fixture

本轮测试不启动图形软件，不创建 Vulkan Window/Device，不依赖 D:/data、公网和人工截图。保留现有回归并补真实输入合约；不以“成功返回”替代数据值、状态和生命周期断言。

## 测试矩阵

| CTest | 责任 | 输入与期望 |
| --- | --- | --- |
| geostudio-globe-tests | 既有空间／单位／图像／Tessellation／TerrainAnchor／Overlay／Texture／生命周期 CPU 回归 | 保留原断言，补入原先未注册的 antimeridian 源文件；默认排除环境和隐藏复现 |
| code-capabilities | Core/Data/Scene/Drivers/Import | Callback 释放、Cache ownership/LRU、SRS 往返和轴序、TileKey、NoData、Map snapshot、字段精度、GeoJSON/OBJ/PNG、TMS 行方向、RGB 高程和错误路径 |
| code-streaming | 私有 Cesium adapter 的输入与资源边界 | JSON 错误恢复、asset version 类型、file URI 精确 bytes、失败后下一请求仍可完成 |
| architecture-contracts | 模块边界 | include 方向、public/private header、UI 与宏残留、源码列表和安装接口 |
| 独立 package consumer | SDK 安装契约 | 禁用 ImGui 查找，公开类型注册，Core/Data 单独链接，私有 GLM 定义不外泄，安装 header 与源码一致 |

P0 先在旧结构上运行三组测试作为基线，再执行源码迁移；宏调整后新增六项 Tile/RGB/GDAL 合约和版本宏编译断言。实际数量与结果以主任务和验证记录为准，不用断言数量替代覆盖说明。

## 小型 fixture

| 文件 | 来源 | 固定期望 |
| --- | --- | --- |
| capabilities/fixtures/polygon.geojson | 本工程人工构造 | 1 个 Polygon、1 个 interior ring、name=parcel、count=7 |
| capabilities/fixtures/empty.geojson | 本工程人工构造 | GDALFeatureSource 报 Invalid extent，featureCount=0；记录当前接口契约 |
| capabilities/fixtures/triangle.obj | 本工程人工构造 | CPU Reader 读出三角形，bounds x=0..2、y=0..3 |
| capabilities/fixtures/pixels.png | 本工程构造的无压缩语义依赖微型 PNG | 2×1 图像，Reader 保留尺寸与数据 |

生成方法与内容身份见 fixture 目录；输入只读。TMS 和高程 RGB 样本直接在代码中以少量 bytes 构造，避免大数据与时间不确定性。部分测试是跨模块 CPU 集成测试，不声称全部为隔离 mock 单元测试。

## 环境用例与暂缓事项

原 HTTPS、Earth File、TMS 网络与 Vertical Datum Grid 断言完整保留。默认离线过滤 `[environment]`、`[requires-grid]`，隐藏 `[.]` 和 `[issue229-repro]` 继续保持隐藏。Vertical Datum 部分独立成 TEST_CASE，避免缺少 us_nga_egm96_15.tif 阻止其他 SRS 回归；不是删除或放宽原期望。

不启用 Slughorn 外部后端、MBTiles Vector 新能力，不覆盖 Vulkan draw、Shader 编译、屏幕拾取画面、LOD 视觉质量、并发渲染退场和性能。这些需要后续独立渲染验收；旧 Demo 人工通过不能代表当前重组产物通过。

OSGB 额外覆盖 CPU Shader 编译及真实 DatabasePager 的工作线程读取／Merge：固定 build fixture 含两个根与同名子文件，验证粗细子图挂接、失败 fallback、关闭拒绝和新 Reader 重开。空 PagedLOD 编译结果必须触发宿主 Viewer 更新。该测试显式提交页请求、使用空 GPU Compile Context，不覆盖视锥选择或真实 GPU 编译，不把队列测试称为完整渲染测试。新增的微型输入复用 `.build/capabilities-r1/tests/capabilities/osgb-fixtures`，仅创建缺失文件并核验已有 bytes。

OSGB 优化回归继续使用小样本，覆盖以下行为；具体运行结果以当前构建输出为准，不以测试数量代替覆盖：

- 索引：真实中文 CP936/GBK SHP 字段、源相对路径与重定位、CRS/原点不一致、二维范围不伪造三维球、内容变化但大小/mtime 相同的缓存失效，以及输出目录/发布锁边界。
- 解码与资源：有界并发文件读取、取消准入/读取、外部页不被隐式展开；不可变资源按内容值复用，并发同键、不同材质状态与 Shader 身份隔离。
- 色彩与纹理：颜色/数值语义隔离，Data/Image/ImageView 格式一致，mip、Origin、Alpha 和压缩格式边界；概览采用 sRGB/Linear 区域滤波及预乘 Alpha，检查透明边缘和源图不变。
- 分页：有限根优先级、根/细节公平调度、关闭丢弃、近期驻留回收、就绪字节与每帧 Merge 预算、实际视口/FOV 调整；不通过删除粗模满足预算。
- 派生与概览：源/外部图片变更失效、路径重绑、半成品发布保护、几何边界保留、预算/取消；SDK `buildDataset` 的 `.osgbset → region → 原始细模` 链保留真实驻留 Draw、纹理及 Options，重复构建复用有效制品；新 Reader 页缓存命中跳过解码，关闭旧 Reader 不影响新分页。

派生测试输出复用 fixture 下的 `index-tests`、`built-dataset`、`derived-pages` 等明确受管位置，失败内容保留；不扫描或转换 HL 全量数据，不启动窗口或 GPU。CPU 解码、Shader 和队列结果仍不能证明首屏时间、颜色像素、LOD 接缝、显存或真实交互性能。

内存与 LOD 的 P0 回归补充：`test_osgb_lod.cpp` 比较实际 OSG PagedLOD 遍历与 VSG Record 选择，覆盖横竖视口、FOV、非对称/正交投影、离轴距离、模型缩放及保守球不影响源范围。`test_osgb_paging.cpp` 覆盖多级纹理、两层关闭重开、读取/编译中取消、未知小页与已知大页预算恢复、父子过渡和各阶段计数归零。`test_resource_maintenance.cpp` 直接观察 allocator 空块析构、并发分配/维护，以及没有后续上传时孤儿数据的释放。上述 GPU 对象使用无 Device 路径；fence、显存与进程峰值仍由 Demo 新包人工验收。

## 执行

在 SDK 根执行 `./scripts/build.ps1 -WithTests` 和 `./scripts/verify-package.ps1`。前者失败即停止安装，后者使用安装包编译。架构检查亦可单独执行 `python -B tests/architecture/check_boundaries.py`。版本／模块变化后先补合约测试，再改实现；失败应修复实际原因，不删除断言或无依据增加容差。

安装 consumer 当前源码在 tests/package-consumer，构建目录为 .build/package-consumer-layout。对全新安装目录追加执行 `python -B tests/architecture/check_package_contents.py --installed <目录>`，检查当前来源闭包、许可证原文和全部 runtime 资源 bytes；该严格检查会报告旧增量安装中遗留的历史文件，不自动清理。

Demo 的文件装配边界测试位于 scripts/test-stage-provenance.ps1，使用明确的新 OutputDirectory 保存微型 fixture，验证当前来源筛选、旧目标残留、patch 篡改和路径越界。不启动渲染，也不覆盖 C++ 单元测试的职责。
