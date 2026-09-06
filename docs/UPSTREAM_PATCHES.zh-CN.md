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

`M7Arc_ItemResults` 记录条目索引、官方操作结果和文件打开错误。Hash handler 校验清单时，它将每个成员映射为通过、缺失、不匹配或错误，保留一次遍历的完整结果；普通提取同样沿用官方错误处理。更新上游时运行 `engine.integration` 中的 SHA256 清单用例验证三种实际结果。

应用的提取回调实现官方 `IArchiveRequestMemoryUseCallback`，将格式 handler 提出的内存请求交给界面。允许继续时返回所需内存额度，取消时返回 `E_ABORT`。`engine.external_modules` 的真实格式模块调用该接口，验证请求字段和继续／取消行为。

## AppleDouble 存储

创建容器归档时，通过 APFS clone／系统复制建立临时输入树，并以 `__MACOSX/<目录>/._<名称>` 保存 AppleDouble 侧车。侧车包含扩展属性、Finder 信息、资源叉、ACL 和创建时间。扫描完成后，数据成员的物理读取路径映射回原文件；临时树负责成员命名、筛选和侧车组织。压缩流因此读取原始所有者、硬链接关系和文件属性。

提取侧车使用独立临时目录。只有实际写入的成员会恢复对应元数据。原归档的 quarantine 属性在元数据恢复后传播到输出成员。重命名、删除操作同步处理关联侧车。

`CreateOptions::preserveMacMetadata` 和 `ExtractOptions::restoreMacMetadata` 默认启用。GZIP、BZIP2、XZ 单流本身无法携带侧车；使用 TAR 组合保留完整目录及 Mac 元数据。

归档内容更新保留归档文件本身的扩展属性、创建时间和权限，包括 quarantine 属性。压缩后删除源文件只在归档完整成功后执行，源文件在压缩期间发生内容或身份变化时保留该文件并报告错误；筛选排除的文件保留。

## 物理输入与逻辑成员路径

文件：

- `third_party/7zip/CPP/7zip/UI/Common/Update.h`
- `third_party/7zip/CPP/7zip/UI/Common/Update.cpp`

`CUpdateOptions::AddPathPrefix` 将逻辑成员前缀传给已有的 `EnumerateItems` 参数，用于绝对路径模式。`IUpdateCallbackUI2::M7Arc_PrepareDirItems` 在扫描后、更新配对前提供一个默认空实现的回调。

M7Arc 在该回调中恢复原输入的所有者、目录时间，并将 `CDirItem::PhyParent` 指向原输入父目录。`LogParent` 保留扫描出的成员路径。因此相对路径、完整路径、绝对路径及归档子目录添加均不包含临时目录名，保存用户 ID／名称时不会写入临时副本的所有者。官方命令行不提供前缀、沿用空回调，行为保持不变。

更新上游时运行 `engine.integration` 验证逻辑路径、筛选、原所有者、成员替换及源文件删除行为。

## ZIP 成员注释的 UTF-8 标志

文件：`third_party/7zip/CPP/7zip/Archive/Zip/ZipHandlerOut.cpp`。

ZIP 的 UTF-8 标志同时覆盖文件名和注释。使用 UTF-8 编码且注释含非 ASCII 字符时设置该标志，使 ASCII 文件名附带中文注释的归档在 Windows 和 macOS 上具有相同解释。成员注释接口仅向实际支持写入此属性的 ZIP 格式开放。

## 内外模块共享方法目录

文件：

- `third_party/7zip/CPP/7zip/UI/Common/LoadCodecs.h`
- `third_party/7zip/CPP/7zip/UI/Common/LoadCodecs.cpp`

`CCodecs::Load` 在启用外部模块时接受一个可选的显式模块路径列表。M7Arc 提供已发现的 macOS 模块路径，跳过独立主 `7z.so` 的默认查找；未传列表的官方命令行行为保持不变。动态库加载失败时记录 `dlerror()` 原始原因。

`EXPORT_CODECS` 分支通过全局 `::CreateEncoder`、`::CreateDecoder`、`::CreateHasher` 工厂与 `::GetMethodProperty` 查询提供内建方法。外部格式可以通过 `SetCodecs` 获得内建和其他外部方法。

模块位置、构建示例和运行时生命周期见 [外部模块](EXTERNAL_MODULES.zh-CN.md)。更新上游时运行 `engine.external_modules` 验证双向方法调用和退出释放。
