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

## 执行

在 SDK 根执行 `./scripts/build.ps1 -WithTests` 和 `./scripts/verify-package.ps1`。前者失败即停止安装，后者使用安装包编译。架构检查亦可单独执行 `python -B tests/architecture/check_boundaries.py`。版本／模块变化后先补合约测试，再改实现；失败应修复实际原因，不删除断言或无依据增加容差。

安装 consumer 当前源码在 tests/package-consumer，构建目录为 .build/package-consumer-layout。对全新安装目录追加执行 `python -B tests/architecture/check_package_contents.py --installed <目录>`，检查当前来源闭包、许可证原文和全部 runtime 资源 bytes；该严格检查会报告旧增量安装中遗留的历史文件，不自动清理。

Demo 的文件装配边界测试位于 scripts/test-stage-provenance.ps1，使用明确的新 OutputDirectory 保存微型 fixture，验证当前来源筛选、旧目标残留、patch 篡改和路径越界。不启动渲染，也不覆盖 C++ 单元测试的职责。
