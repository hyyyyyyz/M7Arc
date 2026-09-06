# 上游源码补丁

基线：7-Zip 26.03，提交 `0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`。

## 提取结果与 macOS 元数据

文件：

- `third_party/7zip/CPP/7zip/UI/Common/ArchiveExtractCallback.h`
- `third_party/7zip/CPP/7zip/UI/Common/ArchiveExtractCallback.cpp`

`CArchiveExtractCallback` 增加 `M7Arc_WrittenIndices` 与 `M7Arc_WrittenPaths` 两个平行数组。在每次归档初始化时清空，并在条目成功提取、输出关闭后记录原始条目索引和实际输出路径。测试、跳过、写入失败、标准输出均不进入记录。

路径来自官方提取流程，因此包含平铺、根目录消除、覆盖时自动改名等处理结果。M7Arc 用它将 AppleDouble 元数据恢复到本次成功写入的文件；覆盖时选择跳过的已有文件不会被修改。

编解码器、格式识别、覆盖策略和路径处理保留上游实现。官方命令行目标也可编译这些字段，未使用记录时行为相同。

更新上游时保留这两个字段、初始化清空和成功写入记录点，再运行 `engine.integration` 验证选择提取、跳过覆盖、自动改名及元数据往返。

## AppleDouble 存储

创建容器归档时，通过 APFS clone／系统复制建立临时输入树，并以 `__MACOSX/<目录>/._<名称>` 保存 AppleDouble 侧车。侧车包含扩展属性、Finder 信息、资源叉、ACL 和创建时间。启用硬链接保留时，根据原输入的设备号与 inode 在临时树中重建链接关系。

提取侧车使用独立临时目录。只有实际写入的成员会恢复对应元数据。原归档的 quarantine 属性在元数据恢复后传播到输出成员。重命名、删除操作同步处理关联侧车。

`CreateOptions::preserveMacMetadata` 和 `ExtractOptions::restoreMacMetadata` 默认启用。GZIP、BZIP2、XZ 单流本身无法携带侧车；使用 TAR 组合保留完整目录及 Mac 元数据。
