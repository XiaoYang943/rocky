# 任务：001-Native SDK 整合 P0

## 基本信息

完整任务标识：geostudio-native／NativeSDK-001

当前阶段：待人工验收

适用规约：公共规约 v1.0、工作区 AGENTS、Rocky 与 Demo 局部规则。

当前方案版本／实施轮次：v1／第 1 轮

当前有效内容身份：SDK build-manifest SHA-256 `ff48b6465c2ba43b3f393a2b02a8115b37297af995d60d26287a26ea80eef072`；制品及验证详见 [P0 验证记录](../P0验证记录.md)。

## 评审与授权记录

用户原文：“放到【D:\code\xiaoyang943\Programming-Lab\GeoStudio\sdk\GeoStudio-Native】，开始实现p0任务”。后续指示：“代码完成后尽快收尾，我手动验证即可”。

范围：保留 Rocky Git 历史与已有修改，迁入指定目录；完整纳入固定 vsgCs／vsgXchange source；来源与 full patches；SDK CMake package／独立 build/install／consumer；现有 TilesetAdapter 迁入 SDK；Demo 改为消费安装包；回归、环境／编译／启动文档。无 Git 暂存、提交、push、远端改名或 merge 历史授权。

MBTiles、统一 Layer API、内部 namespace 全面改名、编辑及性能专项留后续。外部依赖和输入数据只读，不自动升级。

## 仓库与方案

| repoId | 工作副本 | 初始身份与已有差异 | 读写范围 |
| --- | --- | --- | --- |
| geostudio-native | sdk/GeoStudio-Native，原 sdk/rocky | HEAD a4d8b141ec478e8440526928b344c6eb93810631；已有 vector-overlay-picking 修改、技术文档及 Obsidian 未跟踪 | SDK 源码装配、vendor、公共 Adapter、来源和本轮文档；已有设计／技术文档保持 |
| native-gis-demo | demo/native-gis-demo | 独立 unborn Git，69 个既有非忽略文件 | CMake、兼容 header、脚本、当前文档；单个原 Adapter.cpp 实现迁入 SDK |
| workspace | 工作区根 | e8befb90e4663f55345e6d377c7d2adacc408177，开始时干净 | README／manifest／workspace／精确忽略规则及旧 SDK 入口说明 |

根仓库已有源码快照跟踪，因此搬迁在其 status 中体现为旧 SDK 路径删除和新路径出现；实际文件全部迁入新目录，不是批量删除源文件。没有改索引、提交或整改原有双重跟踪策略。独立 SDK 的 .git 与 origin 保留；目录原已存在且为空，核对路径和占用后逐项 Move-Item。

现场文本、hash 与 Git 状态在工作区 `.docs-tmp/native-sdk-r1/baseline.json`。当前源码权威为 SDK provenance，外部依赖按 dependencies.lock.json；Demo 消费 SDK out/install。具体模块、public API、ABI 和生命周期见[架构与迁移](../SDK架构与迁移.md)。

## 知识检索

已读取工作区入口／清单、公共规范／流程／验证矩阵、SDK／Demo AGENTS、模块 README、NativeGIS-002 和相关固定源码／补丁。lookup vsgCs 无命中，不将旧调研等同实测依据。代码与来源依据在 GEOSTUDIO.md／provenance，实际验证在 P0 验证记录。

## 第 1 轮

仓库／工作副本／内容身份：geostudio-native、native-gis-demo、workspace 上述三个副本；起始身份见 baseline.json；最终 SDK manifest `ff48b6465c2ba43b3f393a2b02a8115b37297af995d60d26287a26ea80eef072`。

本轮授权／读写范围：用户授权 P0 与指定目录，范围限上述交付；后续用户明确自行验证界面。已有无关修改保留，不暂存或提交。

| 编号 | 结果 | 证据／定位 | 阻塞原因或不适用理由 |
| --- | --- | --- | --- |
| G1-01 | 通过 | 已读规约、入口、调用方、知识与固定源码，见知识检索 | — |
| G1-02 | 通过 | baseline.json；SDK commit、Demo unborn、根快照关系已登记 | — |
| G1-03 | 通过 | 上述用户授权原文、P0 范围与验收标准 | — |
| G1-04 | 通过 | MBTiles、namespace 全面改名与性能不扩入本轮 | — |
| G2-01 | 通过 | SDK／Demo 实现与完整来源、主任务范围 | — |
| G2-02 | 通过 | 异步退出失败修复与真实数据回归，见验证记录 | — |
| G2-03 | 通过 | Demo 安装包消费、当前文档、README 和清单同步 | — |
| G3-01 | 通过 | SDK build/install／consumer／Demo／13 样本／IPC／Streaming／桌面 Release 日志 | — |
| G3-02 | 通过 | final-scope-audit.json 与来源核验；原有设计／技术文档保护核对 | — |
| G3-03 | 通过 | 失败日志保留；实际 UI 与新机器／跨平台未验证项明确 | — |
| G3-04 | 通过 | regulations-r3.json：62 个本轮文件检查无错误，仅检查单自身待更新；最终 regulations-final.json 复核 | — |
| G4-01 | 待执行 | 用户自行操作新 desktop-native-sdk 包 | AI 不代填人工验收 |
| G4-02 | 通过 | P1／P2 与知识待办已登记 | — |
| G5-01 | 阻塞 | 无暂存；未获得提交批准 | 等待用户明确 Git 授权 |
| G5-02 | 阻塞 | 未创建提交信息或提交 | 等待授权后展示实际暂存范围 |
| G5-03 | 阻塞 | 原 Git 历史保留，未提交／push | 尚无提交授权 |

## 验收标准

| AC | 内容 | 当前状态 |
| --- | --- | --- |
| AC-01 | 指定 SDK 目录、历史、来源及完整源码 | 自检通过 |
| AC-02 | SDK 独立构建／安装、独立 CMake consumer | 自检通过 |
| AC-03 | Demo 使用新 SDK，保持既有样本与生命周期 | 13 样本与 IPC 通过 |
| AC-04 | 指定 3D Tiles 1.0／1.1 | 自动 Streaming／LOD／生命周期通过；画面由用户验收 |
| AC-05 | React 宿主与资源管理器 | 新包启动、嵌入三维球与系统选择器已观察；完整人工流程待用户 |
| AC-06 | 安装、编译、打开软件和边界文档 | 使用指南及验证记录已提供 |

## 人工验收记录

验收结论：待用户验证。

用户原文或可追溯引用：用户要求“我手动验证即可”。旧版本“3dtiles能够加载显示”仅属于旧版本，不自动覆盖本轮身份。

对应内容身份：上述 SDK manifest，native binary `8160da9b63e402a27cfc65769f772e59d3ed9cf66382848da79193babfa6e424`，desktop host `934ead2f72026945bf7026c098b68ca1a4209b90fb6a391e83fe0dc75b25ddf5`。

步骤：打开新包，启动引擎，经资源管理器选择 DNHDL 的 3dtiles10／geostudio-v13 两份 tileset.json，检查几何、纹理、漫游、显隐、移除、重载、停止；原格式按 13 个样本复验。完整路径见[安装使用](../安装构建与使用.md)。

## 知识沉淀与遗留事项

KNOW-01：人工验收后评估 CMake export、GLM／ImGui 隔离和 DLL 退出释放经验；本轮先在设计与验证记录保留，不提前登记为已验收工程知识。代码 impact 对 45 个明确源码／配置路径核对，affected 与 diagnostics 均为空，日志为 `.docs-tmp/native-sdk-r1/knowledge-impact-r2.txt`；未治理条目和隐式依赖不在覆盖内。

P1：MBTiles MVT 和 Demo adapter 收敛，另立任务；P2：Layer API、编辑、namespace 渐进演进及性能。原有 Git 双重快照策略不在本任务整改。

## 本轮交接

当前阶段／等待：待人工验收，按用户指示停止 UI 自动操作。

已完成：P0 源码迁移、独立 SDK、Demo 消费、自动回归、桌面包和使用文档。

待执行或阻塞：用户画面／操作验收；Git 提交未授权；新机器 bootstrap、Debug、跨平台未实测。

下一步：用户打开 out/desktop-native-sdk 的新包验收；后续再评审 MBTiles 或 P1，不自动扩大实现范围。
