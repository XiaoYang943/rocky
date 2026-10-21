# Drivers

子目录：gdal、tms、mbtiles、azure、bing、earth、http/cesium。

输入来源访问与投影、采样适配。TMS 的 row origin 与 URL 模板属于协议；GIS 数据读出后形成 Data/Scene 类型。

依赖边界：依赖 Scene/Data/Core；http/cesium 只由私有 Cesium adapter 编译，不泄漏第三方 C++20 类型。MBTiles/Azure/Bing 沿用可选 feature guard。

验证：GeoJSON fixture、缺失文件、TMS/XYZ URL、高程解码；没有声明新增 MBTiles Vector 支持。

头文件与实现同目录；公开项以 [header 清单](../../cmake/capability-headers.json) 为准。新增源文件同步 source 清单和模块测试，禁止低层反向依赖高层。来源路径通过 [迁移映射](../../provenance/capability-migration.json) 查询。
