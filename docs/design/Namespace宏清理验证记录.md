# Namespace 宏清理验证记录

对应 [NativeSDK-002 第 2 轮](任务/002-Namespace与模块重组.md#第-2-轮)，方案 v3。用户明确授权修改 namespace，同时要求代码中的许可不改。

## 变更

- 297 个源码文件中的 ROCKY_NAMESPACE 改为明确的 geostudio::native，覆盖 namespace 声明、using directive、限定名称及相关注释。
- 删除 Common.h 的 ROCKY_NAMESPACE 定义和 vsg/Common.h 的 ROCKY_VSG_NAMESPACE 定义。当前 src、include、examples、tests、tools、resources、cmake、scripts 中这两个宏均无残留。
- 保持 ROCKY_EXPORT、ROCKY_HAS_* 等功能宏、外部库 namespace、版权及许可内容。
- 同步 Rocky 完整 patch／source map 和顶层 manifest；不改固定来源版本。

宏替换前后编译得到的 namespace 相同，对象布局和实际 ABI 不变，因此保持当前 0.2.0。自行使用这两个旧宏的外部源码需改为明确 namespace。

## 验证

| 检查 | 结果 | 证据 |
| --- | --- | --- |
| 源码精确差异 | 297 文件仅有预期 namespace 替换／两个宏定义移除；leading copyright/license comments 相同 | 工作区 `.docs-tmp/native-sdk-namespace-r2/namespace-audit.json` |
| 许可证文件 | 3 个 LICENSE 文件 bytes 完全一致 | 同上 |
| source map／完整 patch | 固定来源核验、当前文件 hash、reverse apply check 通过 | SDK `out/logs/namespace-r2-build.log` |
| SDK build/install | Windows x64 Release 构建安装通过 | 同上 |
| 独立 installed consumer | C++17 编译、链接、运行通过，检查三个模块、类型注册及 GLM 隔离 | SDK `out/logs/namespace-r2-consumer.log` |
| 知识 impact | 297 个源码路径分 2 批；登记依赖命中 0，diagnostics 0 | 工作区 `.docs-tmp/native-sdk-namespace-r2/impact-result-1.json`、`impact-result-2.json` |

installed build-manifest SHA-256：`4898b3e79f9a738579d4c51b26c2f1bb4b5b4d311dad44de346db9c26577ea45`。

本轮仅做 namespace 宏的等价替换，使用精确差异、完整 SDK 构建和独立 consumer 验证；未重跑完整 Globe 套件或数据／UI 回归。前一轮完整套件的 Vertical Datum case 因缺少 PROJ grid 未通过，仍保留该事实，不宣称本轮解决。Qt 等可选示例的 namespace 已同步，但未构建验收。

本轮没有修改或重新打包 Demo，没有删除文件、修改依赖、暂存、提交或推送。现有桌面包仍对应前一轮制品身份；不能将新 installed manifest 当作旧桌面包身份。

## 差异与交接

当前源码清理结果可通过 include/geostudio/native/globe/Common.h、vsg/Common.h 及全局标识搜索复核。人工验收待用户确认；本轮保留上轮验收和来源历史，不改变原始数据及用户已有修改。
