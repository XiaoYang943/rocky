# 小型 fixture 内容身份

仅用于 CPU 代码合约，本工程人工构造，不来自用户生产数据。GeoJSON 与 OBJ 可直接阅读；PNG 为 2×1、RGB8、首像素红色、次像素绿色，scanline filter=0，以标准 PNG IHDR／IDAT／IEND 与 CRC32 编码。Reader 输出 RGBA8，Alpha=255。

| 文件 | bytes | SHA-256 |
| --- | --- | --- |
| polygon.geojson | 212 | 7e5432858e2189abaa46e58cb36ef4c182a4d73a9faa97dcaf7d262f6a7eda88 |
| empty.geojson | 43 | 6d18edfe75b3149104038dcbca4d0096bbcb923f0f9fa4b49f4815344e75bb90 |
| triangle.obj | 32 | 32dfb0a798728f3cb486b0ca5743a437e28c7594f8a63c630fa23b47407d7278 |
| pixels.png | 72 | 78253200c249d4d84293b2a1e6fc803adedeba8645f38d6bc2f6734a2dfacec9 |

测试不修改 fixture；输入变化须同时解释期望和更新本清单。空 GeoJSON 的 Invalid extent 是当前 SDK 行为，不能宣称读取到了有效空间范围。
