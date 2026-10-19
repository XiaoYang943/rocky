# 任务：002-Namespace 与模块重组

## 基本信息

完整任务标识：geostudio-native／NativeSDK-002

当前阶段：待人工验收

适用规约：v1.0；工作区、SDK 与 Demo AGENTS

当前方案版本／实施轮次：v3／第 2 轮

当前有效内容身份：第 2 轮 GeoStudioNative 0.2.0，installed build-manifest SHA-256 4898b3e79f9a738579d4c51b26c2f1bb4b5b4d311dad44de346db9c26577ea45；验证见 [Namespace 宏清理](../Namespace宏清理验证记录.md)。起始工作树见 `.docs-tmp/native-sdk-namespace-r2/baseline.json`。

第 1 轮内容身份：GeoStudioNative 0.2.0，installed build-manifest SHA-256 38a32928263aba71cd87645d54160e2e0b89ae73f47cdf5c0336cf4650a85dc8；完整制品身份见 [验证记录](../Namespace与模块重组验证记录.md)。起始内容见工作区 `.docs-tmp/native-sdk-r2/baseline.json`；SDK HEAD 为 a4d8b141ec478e8440526928b344c6eb93810631，工作树包含 NativeSDK-001 未提交实现和用户原有修改

## 目标与范围

将接管的 Rocky、vsgXchange、vsgCs 源码改为 geostudio::native 下的真实 namespace，并按 globe、io、tiles3d 组织。公开 header 统一到 include/geostudio/native，形成 GeoStudioNative 0.2 安装包。外部 VSG、Cesium Native、GDAL、PROJ、GLM 保留原 namespace 和固定版本。

不集成新的 Demo 功能。native-gis-demo 仅修改消费接口、构建和运行资源定位，保持既有数据行为。保留官方示例与来源映射，移除被新模块构建取代的入口和未使用的上游 CI／依赖编排文件。用户资料、原有设计文档、Git 历史、许可证、必要资源与旧制品保留。

## 知识检索

lookup Rocky 命中无关资料 URL，不采用；vsgCs、vsgXchange 无命中。依据当前固定源码、NativeSDK-001 验证记录及本轮读取的 CMake／类型注册／资源查找实现。未将官方示例存在等同为项目验证通过。

## 评审与授权记录

用户原文：“1. demo先不集成 2. 我赋予你权限，该删除的删除 3. 先改namespace和重组模块代码”。本轮据此执行 v2：暂缓示例集成，优先源码重组、namespace 改名和必要清理。删除限定核对过的明确文件，逐文件记录；无暂存、提交、推送授权。

## 第 1 轮

仓库／工作副本／内容身份：geostudio-native 位于 sdk/GeoStudio-Native，native-gis-demo 位于 demo/native-gis-demo；两者起始内容与已有差异见 baseline.json。工作区根已有嵌套源码快照，不调整该历史 Git 策略。

本轮授权／读写范围：上述用户原文；SDK 模块、include、CMake、脚本、来源映射和本轮文档；Demo 的 API 消费与资源／打包适配。只读外部依赖与验收原件。已有 vector-overlay-picking.md、技术文档、IDE 配置原样保留。

| 编号 | 结果 | 证据／定位 | 阻塞原因或不适用理由 |
| --- | --- | --- | --- |
| G1-01 | 通过 | 已读规约、SDK／Demo AGENTS、接口、构建、来源与知识 | — |
| G1-02 | 通过 | baseline.json，现有 NativeSDK-001 与用户修改已登记 | — |
| G1-03 | 通过 | 上述 v2 用户授权与范围 | — |
| G1-04 | 通过 | 无新 Demo 功能，不改外部依赖 namespace | — |
| G2-01 | 通过 | 三个真实 namespace；src/globe、io、tiles3d 和统一 include | — |
| G2-02 | 通过 | installed consumer 检查类型注册；13 样本、IPC 与两份 Tileset 生命周期回归通过 | — |
| G2-03 | 通过 | Demo 最小消费适配；source map／patch 和安装使用文档已维护 | — |
| G3-01 | 通过 | SDK build/install、consumer、13 样本、IPC 和两份 Tileset 通过；完整 Globe 套件存在已登记环境失败，见验证记录 | — |
| G3-02 | 通过 | module-migration.json 与 baseline audit：29 个既有文档原样保留，无未解释缺失 | — |
| G3-03 | 通过 | 验证记录明确 SRS grid 缺失、HTTPS 依赖及未验证 UI／可选示例 | — |
| G3-04 | 通过 | 本轮规约检查与差异核对；tests.cpp 超工具 diff 容量，单独核对增量仅 include／namespace／pool name；二进制迁移由 source map 核验 | — |
| G4-01 | 待执行 | 用户手动验收 | 不由 AI 代填 |
| G4-02 | 待执行 | impact 已分 5 批执行，登记依赖命中 0、diagnostics 0；人工验收后评估沉淀 | 125 条尚未治理条目不作为已验证依据 |
| G5-01 | 阻塞 | 未暂存 | 尚无提交授权 |
| G5-02 | 阻塞 | 未提交 | 尚无提交授权 |
| G5-03 | 阻塞 | 未 push | 尚无推送授权 |

## 第 2 轮

仓库／工作副本／内容身份：geostudio-native，sdk/GeoStudio-Native 独立 Git 根；本轮起始 HEAD、工作树文件与 hash 见 `.docs-tmp/native-sdk-namespace-r2/baseline.json`。已有反经线等其他修改保持，只定向替换 namespace 标识。

本轮授权／读写范围：用户原文“1.命名空间修改掉 2. 代码中的许可就不改了”。v3 只替换 SDK include、src、examples、tests 中的 ROCKY_NAMESPACE／ROCKY_VSG_NAMESPACE 引用、删除相应宏定义，并同步来源 patch／manifest 和本任务记录。版权、许可、第三方依赖及其他功能宏不改；不新增 Demo 功能，不修改 Demo 源码，不暂存、提交或推送。

知识检索：lookup ROCKY_NAMESPACE，包含候选，命中 0；根据 Common.h 的宏展开、所有引用和固定来源实施。宏展开前后实际 namespace 与对象布局相同，保持当前 0.2.0 ABI；依赖这两个旧宏的源码需改用明确 namespace。

| 编号 | 结果 | 证据／定位 | 阻塞原因或不适用理由 |
| --- | --- | --- | --- |
| G1-01 | 通过 | 已读工作区／SDK 规则、主任务、源码与来源约定 | — |
| G1-02 | 通过 | 本轮 baseline 与现有 source map 一致，保存已有差异 | — |
| G1-03 | 通过 | 上述用户原文授权 v3 | — |
| G1-04 | 通过 | 仅 namespace 宏，不变对象布局、许可和 Demo 功能 | — |
| G2-01 | 通过 | 297 个文件改为明确 namespace，两个 namespace 宏定义移除 | — |
| G2-02 | 通过 | 精确字节差异核对通过，代码许可注释与 3 个 LICENSE 文件未改 | — |
| G2-03 | 通过 | 完整来源 patch／manifest 更新并通过 verify-sources | — |
| G3-01 | 通过 | namespace-r2-build.log 与 namespace-r2-consumer.log：构建安装、独立链接运行通过 | — |
| G3-02 | 通过 | 当前源码及安装 headers 无旧 namespace 宏残留，差异均为本轮授权范围 | — |
| G3-03 | 通过 | 第 2 轮验证记录注明没有重新打包 Demo／执行全量数据或 UI 回归；历史 PROJ grid 失败保留 | — |
| G3-04 | 通过 | 规约检查及增量核对；生成的来源记录由 verify-sources 检查，大文件精确替换另有字节级核对 | — |
| G4-01 | 待执行 | 用户验收 | 不由 AI 代填 |
| G4-02 | 不适用 | impact 两批均无登记依赖命中；等价 namespace 宏清理保留在主任务，无新增可复用知识结论 | 无新增知识条目必要 |
| G5-01 | 阻塞 | 未暂存 | 无 Git 暂存／提交授权 |
| G5-02 | 阻塞 | 未提交 | 无提交授权 |
| G5-03 | 阻塞 | 未推送 | 无推送授权 |

## 验收标准与验证计划

1. 公开 API 使用新 namespace 与 include，无旧 namespace alias 伪装改名；CMake targets 为 GeoStudio::Globe、IO、Tiles3D、Native。
2. 独立安装包 consumer 编译／链接／执行，验证类型注册与 GLM 隔离。
3. Demo 仅适配新 SDK；13 样本、IPC、两份 DNHDL Tileset 的既有回归通过；不做额外 UI 自动操作。
4. 来源映射覆盖迁移及删除，固定 upstream hash、完整 patch 与当前文件身份可核验。
5. 环境、编译、运行入口和兼容边界明确；旧 ABI 安装包保持独立。

## 人工验收记录

验收结论：待用户验证；旧版本人工结论不覆盖当前重构。

## 知识沉淀与遗留事项

已准备本轮源码差异清单并执行 impact；5 批共 1244 项变更路径（移动的旧路径与新路径分别核对），登记依赖命中 0，diagnostics 0；125 条尚未治理条目未作为已验证依据。报告位于工作区 `.docs-tmp/native-sdk-r2/knowledge-impact-summary.json`。人工验收后再评估 namespace／ABI 迁移经验，当前不将自动检查写成已验收工程知识。示例新功能、MBTiles、编辑和性能不在本轮。

## 本轮交接

当前阶段／等待：待人工验收

已完成：第 2 轮 namespace 宏清理，版权／许可保持；来源更新、精确差异核对、SDK 构建安装与独立 consumer 编译运行通过。

待执行或阻塞：用户手动验收；完整 Globe 套件 1 个 Vertical Datum case 因本机缺少所需 PROJ grid 未通过；未授权 Git 提交、推送。

下一步：用户复核 namespace 清理及许可保留；本轮结果和身份见 [Namespace 宏清理验证记录](../Namespace宏清理验证记录.md)。详细结果、限制和内容身份见 [验证记录](../Namespace与模块重组验证记录.md)。
