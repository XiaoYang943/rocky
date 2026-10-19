# vsgCs 架构与数据链路调研

## 基本信息

| 项目 | 内容 |
| --- | --- |
| 完整任务标识 | rocky／技术文档-003 |
| 类型／优先级 | 文档与静态源码调研；用户指定增量 |
| 当前阶段 | 待人工验收 |
| 等待／阻塞 | 无文档实施阻塞；待用户对本轮内容反馈，未批准代码接入或构建 |
| 适用规约 | workspace v1.0、Rocky AGENTS、英文术语与文件保护要求 |
| 主责模块 | [调研分析/vsgCs](../调研分析/vsgCs/README.md) |
| 方案版本／实施轮次 | v1；第 1 轮 |
| 当前内容身份 | 九份交付文档的路径/SHA-256 集合由工作区 `.docs-tmp/vsgcs-architecture-r1/verification.json` 登记，不沿用任务 002 的旧检查身份 |
| 关联任务 | [002 四库调研](002-渲染基础库架构调研.md)，原任务与报告保持 |

## 背景、目标、方案与授权

用户要求“再梳理【https://github.com/timoore/vsgCs】到【\rocky\docs\技术文档\调研分析\vsgCs】”。这直接授权本轮文档与来源调研；用户既有代码修改前确认约束继续适用，本轮没有修改应用、Demo 或第三方实现。

方案 v1：固定 vsgCs Commit 和所声明的 Cesium Native 来源，按“公开入口 → 默认装配 → 内容/资源链 → Update/渲染 → 释放/失败 → 数据能力和接入建议”展开。对 UI 和数据支持追到代码，不将 README/Roadmap 或未运行样例当作本机能力证明。独立交付三份专题文档、一份主任务，定向更新五份现有导航/术语。

目标是说明 vsgCs 在 Rocky/VSG 桌面路线中的位置，尤其是 3D Tiles、Quantized Mesh、PNTS 与六大类原始数据的区别。无需新 Reader、宿主或兼容补丁；不克隆到 SDK、安装依赖、修改来源锁或读取实际 DNHDL 数据。Bug 修复/复现不适用，本轮是架构和接口调研。

已读取 workspace README/manifest、公共规范、开发流程、文档规范、验证矩阵、任务模板、Rocky AGENTS、技术文档入口和任务 002；只读 Demo 规则及消费锁。用户明确指定技术文档位置优先于上游一般 `docs/design` 约定。没有启用额外 Skill 或委派子代理。

## 知识检索

| 查询 | 结果与实际阅读 | 采用理由 |
| --- | --- | --- |
| `vsgCs`，含候选 | 无命中 | 固定上游源码作为本库依据 |
| `Cesium Native`，含候选 | 两条尚未治理命中：3D Tiles 规范英文提取前置页与 OSGB 标准协议解析 Cesium 段落；已读附近内容 | 前者无本库实现信息，后者只作背景线索；不采作固定 API、兼容或性能依据 |

具体路径、行段与 Hash 记录在 `knowledge-review.json`。本轮不修改知识库；源码观察保留在本专题，待内容反馈及真实功能验收后再评估可复用知识，避免复制未验证工程结论。

## 副本、基线与读写范围

| 副本 | 初始身份与差异 | 本轮读写与实际消费 |
| --- | --- | --- |
| workspace | HEAD `ae878b8ac0023c36c7dc216353fd950b16b0d964`；62 个跟踪文件，已有用户修改及未跟踪资料 | 规范/知识/清单只读；仅新增 `.docs-tmp/vsgcs-architecture-r1` 证据 |
| `sdk/rocky` | HEAD `a4d8b141ec478e8440526928b344c6eb93810631`；421 个跟踪文件；原有 vector-overlay-picking 修改、Obsidian 与技术文档未跟踪 | 只改本次九份文档；原有源码、设计、任务与其他正文不覆盖 |
| Demo | 现有 sources.lock 固定 VSG、vsgXchange、Rocky，无 vsgCs/Cesium Native 消费项 | 来源锁与 `.deps` 只读，消费不变 |
| vsgCs 来源快照 | master HEAD 固定为 `72176fdfb35acb8462c5760c1b432d72f19c767a` | 明确文本文件保存在证据目录，不是 SDK 安装或完整 Git Clone |
| Native 来源快照 | vsgCs Port 指定 0.58.0；Tag 对应 `56840e6ce2c8d0ff50a46f8c09ddae52c29d5e6f` | 只读取选定依赖实现，不据此声称完整构建组合已验证 |

`baseline.json` 保留两 Git 根、20 份既有技术 Markdown 原文/Hash、计划新文件及受保护文件。获取源码通过只读 HTTP 取明确文件，核对 Git Blob 与 SHA-256；不运行上游脚本、不应用补丁、不批量删除或清理。

Rocky 仍未在 workspace-manifest 登记 repoId；本轮不改配置或伪装来源来运行统一规约工具，采用文档和差异定向检查并说明限制。

## 交付范围

| 状态 | 文件 | 用途 |
| --- | --- | --- |
| 新增 | [vsgCs README](../调研分析/vsgCs/README.md) | 模块、主链、线程/资源、坐标、Overlay、UI 与限制 |
| 新增 | [数据能力与接入建议](../调研分析/vsgCs/数据能力与接入建议.md) | 六类数据、Tile 协议、两条 3D Tiles 路线、优先级与最小验收 |
| 新增 | [来源与构建边界](../调研分析/vsgCs/来源与构建边界.md) | Commit、版本差异、构建要求、来源身份和 C/N 证据 |
| 新增 | 本主任务 | 范围、授权、自检、人工验收与交接 |
| 定向修改 | [技术文档入口](../README.md)、[调研入口](../调研分析/README.md)、[公共来源页](../调研分析/来源与版本.md) | 增量导航、关系图与版本/任务边界 |
| 定向修改 | [术语](../术语.md) | 保留原 124 组定义，新增 H 组英文术语与来源 |
| 定向修改 | [任务索引](README.md) | 登记任务 003 当前阶段 |

## 验收标准与验证计划

| AC | 可判定标准 | 验证 |
| --- | --- | --- |
| AC-01 | 指定目录中有架构、数据能力和来源文档，导航有效 | TC-01 |
| AC-02 | 区分 Native Selection/Preparer 与 vsgXchange PagedLOD/DatabasePager，追到实际装配与调用 | TC-02 |
| AC-03 | 明确 PNTS/Quantized Mesh 的现成路线、GLB 2 限制、六类原始数据边界及后续优先级 | TC-02 |
| AC-04 | Qt、ImGui 依赖、React 宿主、Device/View/坐标与生命周期不被写成已集成能力 | TC-02、TC-04 |
| AC-05 | 原术语定义和范围外文件保持，文档无具体日历日期，未执行项真实 | TC-01、TC-03、TC-04 |
| AC-06 | 固定来源可追溯，旧任务/报告不被冒作本轮通过证明 | TC-02、TC-03 |

| TC | 检查内容与环境 | 写入/限制 |
| --- | --- | --- |
| TC-01 | Node 只读检查技术文档 UTF-8、本地链接/Anchor；九份交付的结构、Fence、日期、空白和 JSON 示例 | 报告只写新证据目录；不等于视觉/GPU 验收 |
| TC-02 | 对照核心定义、实现、调用者；固定 Commit 的源码 Blob/Hash，关键文本及外链对应文件检查 | 文本符号检查不等于完整 AST/调用图审计；取得文件不等于所有行均已分析 |
| TC-03 | 跟踪文件/HEAD/状态、原 20 份文档、原术语定义、锁和 Obsidian 保护项与 baseline 对照 | 范围外变化单列，不恢复用户或外部应用内容 |
| TC-04 | 人工核对证据层级、版本差异、原始/预处理输入、建议/现状与未执行项 | AI 自检不代填人工验收 |

## 关键观察与实现边界

- 默认 Runtime 使用 `CesiumCurl::CurlAssetAccessor`，旧 `UrlAssetAccessor` 文件不是当前默认调用链；`RuntimeEnvironment::update` 为空，Tileset 更新由 Viewer Operation 驱动。
- vsgCs CMake 声明 1.1.0，Manifest 仍为 0.9.0；版本差异原样登记。Native 0.58.0 的 PNTS Converter 与 vsgCs Point Pipeline 形成明确路线，Quantized Mesh 则走 `layer.json` Loader。
- Native B3DM 支持 Legacy Header 分支，但 GltfReader 拒绝非 2 的 GLB 版本；不能先声称此路线修复任意旧数据。
- 不要求 Qt；当前上游 CMake 仍强制 ImGui/vsgImGui，React UI 需要原生宿主连接。Rocky 同场景接入、Reader 顺序、Windows URI、View/Transform、异步销毁均有待实际验证。
- 原始 Feature、GeoTIFF/DEM 继续参考 Rocky；OSGB/LAS/LAZ 直读与完整编辑不属于本次已确认直接能力。

上述是固定源码观察与接入建议，详细证据见专题；本轮没有借此整改上游问题或实现新功能。

## 第 1 轮检查单、验证与人工验收

- [x] 确认用户指定落点与文档授权，明确方案 v1 和不改变消费的范围。
- [x] 读取规约/知识/原任务，固定两组远端来源与本地初始身份。
- [x] 完成三份专题正文与五份导航/术语的定向增量，保留旧任务和报告。
- [x] 核查主链、格式、UI/坐标/资源边界；建议与实际验证分开。
- [x] 完成最终自检与九份文档内容身份，转待人工验收。
- [ ] 用户对本次明确内容身份进行人工验收。
- [x] 本轮不暂存、提交、推送，不构建、安装或运行 GPU/数据用例。

定向自检通过：核对九份交付的 UTF-8、结构、Fence、日期、空白和 JSON 示例，并检查关联技术文档的本地链接/Anchor；新增 15 组术语，原 124 组定义全部保留。79 份 vsgCs 与 16 份 Native 快照的 Git Blob/Hash 一致，39 项关键源码文本检查通过。上述是文档和静态源码检查，不是完整 AST/调用图、兼容、构建或性能测试。

workspace 的 62 个跟踪文件、Rocky 的 421 个跟踪文件及两个 HEAD 保持原身份；既有技术文档只改五份指定导航/术语，旧任务 001/002 与库正文不变；Demo 来源锁和 OSG manifest 不变。观察到 Obsidian `docs/.obsidian/workspace.json` 出现范围外变化，本轮未写入/恢复，未调查具体进程来源，报告单列前后 Hash。

结果及最终内容身份维护在工作区 [.docs-tmp/vsgcs-architecture-r1/verification.json](../../../../../.docs-tmp/vsgcs-architecture-r1/verification.json)，不嵌入自引用 Hash。未运行统一规约工具（Rocky 未登记）、新的构建/安装、worldviewer、GPU/数据测试、Mermaid 渲染或 Git 写操作。源码快照属于来源证据，不属于受当前 Demo 消费的实现。

人工验收建议：先读架构与数据矩阵，再确认原始数据/Tile、两个 3D Tiles 后端、React/ImGui 边界以及后续优先级是否符合预期。验收结论：待用户；AI 未填写通过。后续内容变化需重算身份和复核。

## 知识沉淀、提交与交接

本轮结论保留在工程调研，不复制成新的知识库正式条目；原因是固定版本工程观察及接入建议尚未经过功能验证，现有文档已经是权威入口。用户验收后再按实际复用需求评估沉淀，升级依赖时复核关联结论。

没有 Git 写操作、推送或发布。下一关卡是本轮文档人工验收；若进入独立构建、具体 DNHDL 兼容定位或 Rocky/React 宿主接入，应明确新的代码/环境范围后实施。恢复上下文先读本任务、专题入口、来源页和本轮报告，不修改 Cache 或切换依赖来恢复调研状态。
