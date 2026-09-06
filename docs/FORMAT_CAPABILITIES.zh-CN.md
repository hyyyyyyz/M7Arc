# 格式、编码器与创建能力

M7Arc 的格式范围以随项目保存的 **7-Zip 26.03 完整引擎**为基线：**61 个 handler、7 类归档/流/镜像写入格式、25 个注册 codec、10 个注册 hasher**。61 个 handler 包含 Hash；Hash 的清单生成能力另计，不占用七类归档写入的名额。数据来自 [Apple Silicon 引擎能力记录](evidence/7zz-info.txt)、完整构建目标与实际注册代码。

应用的“打开”入口覆盖全部 handler；“添加到归档”提供七类真实写入格式和三个 TAR 组合预设；“CRC / SHA”负责计算、生成与验证摘要。格式中心同时展示扩展名、读取能力、写入能力、类型和具体限制。正式 Qt 应用从 `CCodecs` 枚举当前随包引擎，界面根据返回的属性构造选项。

## Windows 创建菜单与引擎能力

[UpdateGUI.cpp](../third_party/7zip/CPP/7zip/UI/GUI/UpdateGUI.cpp) 的格式选择同时检查 `UpdateEnabled` 与 `Flags_KeepName()`。原版针对单个普通文件可以给出七类创建格式；目录或多个文件会排除 GZip、BZip2、XZ 三种单流格式，留下 7z、ZIP、TAR、WIM。Hash 从普通添加菜单排除，仅在显式选择 Hash 的入口中加入。

[CompressDialog.cpp](../third_party/7zip/CPP/7zip/UI/GUI/CompressDialog.cpp) 中 `g_Formats` 定义八组专用控件，其中七组是实际归档创建，另一组是 Hash。方法数组还须经过 `SetMethod2()` 筛选：`g_7zMethods` 列出七种方法，而普通 7z 下拉仅显示 LZMA2、LZMA、PPMd、BZip2；Copy 由存储级别选择，Deflate / Deflate64 可经参数使用。

| 创建格式 | Windows 普通方法选择 | 完整引擎编码能力 | 原版级别 | 文件组织 | 加密与其他控件 |
|---|---|---|---|---|---|
| 7z | LZMA2、LZMA、PPMd、BZip2 | 前四种 + Deflate、Deflate64、Copy | 0–9，各整数级别 | 多文件、目录；固实/非固实 | AES-256、文件名加密、固实块、线程、滤镜、字典、单词大小/模型阶数、内存估算 |
| ZIP | Deflate、Deflate64、BZip2、LZMA、PPMd | 前五种 + Copy | 0、1、3、5、7、9 | 多文件、目录 | ZipCrypto / AES-256、线程；无文件名加密、无固实块 |
| GZip | Deflate | Deflate | 1、5、7、9 | 单个文件流 | 固定 32 KiB 字典、单词大小、内存估算 |
| BZip2 | BZip2 | BZip2 | 1、3、5、7、9 | 单个文件流 | 块大小、线程、内存估算 |
| XZ | LZMA2 | LZMA2；可指定 XZ 预处理滤镜 | 1–9，各整数级别 | 单个文件流；可分多个内部块 | 字典、单词大小、流内块大小、线程、完整性检查类型 |
| TAR | GNU、POSIX | 存储；选择 TAR 头格式 | 0 | 多文件、目录、链接元数据 | GNU/POSIX 是头格式选项，不是压缩算法 |
| WIM | 存储 | Copy | 0 | 文件树与镜像结构 | 新增数据按 Store 写入；读取端另外支持压缩资源 |

0 级表示 Store。7z/ZIP 的算法控件在 0 级随之停用；TAR 保留 GNU/POSIX 选择。原版 Hash 专用方法列表为 SHA256、SHA1，M7Arc 的独立摘要页面同时提供当前引擎注册的十种计算算法。

### TAR 组合预设

| 预设 | 实际处理链 | 关联别名 | 编辑方式 |
|---|---|---|---|
| `.tar.gz` | 文件树 → TAR → GZip | `.tgz`、`.tpz` | 更新 TAR 内容后重新编码外层 |
| `.tar.bz2` | 文件树 → TAR → BZip2 | `.tbz`、`.tbz2` | 更新 TAR 内容后重新编码外层 |
| `.tar.xz` | 文件树 → TAR → XZ | `.txz` | 更新 TAR 内容后重新编码外层 |

这些预设复用两个现有 handler，不增加格式数量。界面明确显示两步处理与当前阶段，并允许展开 TAR 头格式和外层压缩参数。选中多个文件后，原生 `.gz`、`.bz2`、`.xz` 选择项显示单流约束；组合预设保持可选。`.tzst` / `.tar.zst` 可以打开，但不提供创建预设，因为当前基线没有 Zstd 编码端。

## 全部 61 个 handler

表中的“读取”指该 handler 能打开其支持的容器、列出可公开条目并执行其提供的提取/测试操作。磁盘和文件系统 handler 读取镜像内容；可执行文件 handler 浏览内部区段或嵌入内容。Hash 的读取操作是列出与验证摘要记录。格式中心为这些类别使用不同说明。

| # | Handler | 扩展名 / 别名 | 类型 | 读取 | 写入 | 行为 |
|---|---|---|---|---|---|---|
| 1 | [7z](../third_party/7zip/CPP/7zip/Archive/7z/7zRegister.cpp) | .7z | 归档与软件包 | 是 | 创建/条件更新 | 可创建、浏览、提取和更新；支持固实压缩与 AES-256 文件名加密。 |
| 2 | [APFS](../third_party/7zip/CPP/7zip/Archive/ApfsHandler.cpp) | .apfs、.img | 文件系统 | 是 | — | 读取 APFS 文件系统镜像；不能将本机 APFS 卷当作归档直接写入。 |
| 3 | [APM](../third_party/7zip/CPP/7zip/Archive/ApmHandler.cpp) | .apm | 磁盘与分区 | 是 | — | 读取 Apple Partition Map 分区表及分区内容。 |
| 4 | [Ar](../third_party/7zip/CPP/7zip/Archive/ArHandler.cpp) | .ar、.a、.deb、.udeb、.lib | 归档与软件包 | 是 | — | 读取 ar 静态库与 Debian 软件包容器。 |
| 5 | [Arj](../third_party/7zip/CPP/7zip/Archive/ArjHandler.cpp) | .arj | 归档与软件包 | 是 | — | 读取 ARJ 归档。 |
| 6 | [Base64](../third_party/7zip/CPP/7zip/Archive/Base64Handler.cpp) | .b64 | 压缩与编码流 | 是 | — | 解码 Base64 文本流；按扩展名或指定格式打开。 |
| 7 | [COFF](../third_party/7zip/CPP/7zip/Archive/PeHandler.cpp) | .obj | 可执行文件与固件 | 是 | — | 浏览、提取 COFF 目标文件内部内容。 |
| 8 | [Cab](../third_party/7zip/CPP/7zip/Archive/Cab/CabRegister.cpp) | .cab | 归档与软件包 | 是 | — | 读取 Microsoft Cabinet，含分卷与多种内置解码方法。 |
| 9 | [Chm](../third_party/7zip/CPP/7zip/Archive/Chm/ChmHandler.cpp) | .chm、.chi、.chq、.chw | 文档与媒体 | 是 | — | 读取编译 HTML 帮助及关联容器。 |
| 10 | [Compound](../third_party/7zip/CPP/7zip/Archive/ComHandler.cpp) | .msi、.msp、.msm、.doc、.xls、.ppt、.aaf | 文档与媒体 | 是 | — | 读取 OLE Compound 容器；文档后缀对应容器结构浏览。 |
| 11 | [Cpio](../third_party/7zip/CPP/7zip/Archive/CpioHandler.cpp) | .cpio | 归档与软件包 | 是 | — | 读取 CPIO 多种头格式。 |
| 12 | [CramFS](../third_party/7zip/CPP/7zip/Archive/CramfsHandler.cpp) | .cramfs | 文件系统 | 是 | — | 读取 CramFS 只读文件系统镜像。 |
| 13 | [Dmg](../third_party/7zip/CPP/7zip/Archive/DmgHandler.cpp) | .dmg | 磁盘与分区 | 是 | — | 读取 Apple DMG 容器及内层镜像，支持情况由容器实际压缩方法决定。 |
| 14 | [ELF](../third_party/7zip/CPP/7zip/Archive/ElfHandler.cpp) | .elf | 可执行文件与固件 | 是 | — | 浏览、提取 ELF 内部内容。 |
| 15 | [Ext](../third_party/7zip/CPP/7zip/Archive/ExtHandler.cpp) | .ext、.ext2、.ext3、.ext4、.img | 文件系统 | 是 | — | 读取 Ext 系列文件系统镜像。 |
| 16 | [FAT](../third_party/7zip/CPP/7zip/Archive/FatHandler.cpp) | .fat、.img | 文件系统 | 是 | — | 读取 FAT 文件系统镜像。 |
| 17 | [FLV](../third_party/7zip/CPP/7zip/Archive/FlvHandler.cpp) | .flv | 文档与媒体 | 是 | — | 浏览、提取 Flash Video 内部流。 |
| 18 | [GPT](../third_party/7zip/CPP/7zip/Archive/GptHandler.cpp) | .gpt、.mbr | 磁盘与分区 | 是 | — | 读取 GUID Partition Table 及分区内容。 |
| 19 | [HFS](../third_party/7zip/CPP/7zip/Archive/HfsHandler.cpp) | .hfs、.hfsx | 文件系统 | 是 | — | 读取 HFS/HFS+ 系列文件系统镜像。 |
| 20 | [Hxs](../third_party/7zip/CPP/7zip/Archive/Chm/ChmHandler.cpp) | .hxs、.hxi、.hxr、.hxq、.hxw、.lit | 文档与媒体 | 是 | — | 读取 Microsoft Help 2 与相关容器。 |
| 21 | [IHex](../third_party/7zip/CPP/7zip/Archive/IhexHandler.cpp) | .ihex | 压缩与编码流 | 是 | — | 读取 Intel HEX 编码流。 |
| 22 | [Iso](../third_party/7zip/CPP/7zip/Archive/Iso/IsoRegister.cpp) | .iso、.img | 磁盘与分区 | 是 | — | 读取 ISO 光盘镜像；同一扩展名也可能由 UDF 处理。 |
| 23 | [LP](../third_party/7zip/CPP/7zip/Archive/LpHandler.cpp) | .lpimg、.img | 磁盘与分区 | 是 | — | 读取 Android Logical Partition 镜像。 |
| 24 | [Lzh](../third_party/7zip/CPP/7zip/Archive/LzhHandler.cpp) | .lzh、.lha | 归档与软件包 | 是 | — | 读取 LZH/LHA 归档。 |
| 25 | [MBR](../third_party/7zip/CPP/7zip/Archive/MbrHandler.cpp) | .mbr | 磁盘与分区 | 是 | — | 读取 Master Boot Record 分区表及分区内容。 |
| 26 | [MachO](../third_party/7zip/CPP/7zip/Archive/MachoHandler.cpp) | .macho | 可执行文件与固件 | 是 | — | 浏览、提取 Mach-O 内部内容。 |
| 27 | [MsLZ](../third_party/7zip/CPP/7zip/Archive/MslzHandler.cpp) | .mslz | 压缩与编码流 | 是 | — | 读取 Microsoft SZDD 压缩流。 |
| 28 | [Mub](../third_party/7zip/CPP/7zip/Archive/MubHandler.cpp) | .mub | 可执行文件与固件 | 是 | — | 读取 Mach-O Universal Binary 容器与架构切片。 |
| 29 | [NTFS](../third_party/7zip/CPP/7zip/Archive/NtfsHandler.cpp) | .ntfs、.img | 文件系统 | 是 | — | 读取 NTFS 文件系统镜像及其元数据。 |
| 30 | [Nsis](../third_party/7zip/CPP/7zip/Archive/Nsis/NsisRegister.cpp) | .nsis | 可执行文件与固件 | 是 | — | 读取 NSIS 安装包；可在 EXE 中定位嵌入容器。 |
| 31 | [PE](../third_party/7zip/CPP/7zip/Archive/PeHandler.cpp) | .exe、.dll、.sys | 可执行文件与固件 | 是 | — | 浏览、提取 PE 内部内容；可进一步打开嵌入归档。 |
| 32 | [Ppmd](../third_party/7zip/CPP/7zip/Archive/PpmdHandler.cpp) | .pmd | 压缩与编码流 | 是 | — | 读取独立 PPMd 流；7z 内 PPMd 编码能力不意味着可创建 .pmd 文件。 |
| 33 | [QCOW](../third_party/7zip/CPP/7zip/Archive/QcowHandler.cpp) | .qcow、.qcow2、.qcow2c | 磁盘与分区 | 是 | — | 读取 QCOW 系列虚拟磁盘镜像。 |
| 34 | [Rar](../third_party/7zip/CPP/7zip/Archive/Rar/RarHandler.cpp) | .rar、.r00 | 归档与软件包 | 是 | — | 读取旧版 RAR 与分卷；不提供 RAR 编码器。 |
| 35 | [Rar5](../third_party/7zip/CPP/7zip/Archive/Rar/Rar5Handler.cpp) | .rar、.r00 | 归档与软件包 | 是 | — | 读取 RAR5 与分卷；按签名和内容区分 Rar/Rar5。 |
| 36 | [Rpm](../third_party/7zip/CPP/7zip/Archive/RpmHandler.cpp) | .rpm | 归档与软件包 | 是 | — | 读取 RPM 软件包外层并进入内层压缩流和 CPIO。 |
| 37 | [SWF](../third_party/7zip/CPP/7zip/Archive/SwfHandler.cpp) | .swf | 文档与媒体 | 是 | — | 读取未压缩 Flash SWF 内容。 |
| 38 | [SWFc](../third_party/7zip/CPP/7zip/Archive/SwfHandler.cpp) | .swf | 文档与媒体 | 是 | — | 读取压缩 Flash SWF；与 SWF 共享后缀而使用不同 handler。 |
| 39 | [Sparse](../third_party/7zip/CPP/7zip/Archive/SparseHandler.cpp) | .simg、.img | 磁盘与分区 | 是 | — | 读取 Android sparse 镜像并提供展开后的虚拟流。 |
| 40 | [Split](../third_party/7zip/CPP/7zip/Archive/SplitHandler.cpp) | .001 | 分卷 | 是 | — | 读取 .001 起始的原始分割文件组；分割/合并是文件操作。 |
| 41 | [SquashFS](../third_party/7zip/CPP/7zip/Archive/SquashfsHandler.cpp) | .squashfs | 文件系统 | 是 | — | 读取 SquashFS 文件系统镜像。 |
| 42 | [TE](../third_party/7zip/CPP/7zip/Archive/PeHandler.cpp) | .te | 可执行文件与固件 | 是 | — | 浏览、提取 Terse Executable 内部内容。 |
| 43 | [UEFIc](../third_party/7zip/CPP/7zip/Archive/UefiHandler.cpp) | .scap | 可执行文件与固件 | 是 | — | 读取 UEFI Capsule 容器。 |
| 44 | [UEFIf](../third_party/7zip/CPP/7zip/Archive/UefiHandler.cpp) | .uefif | 可执行文件与固件 | 是 | — | 读取 UEFI Firmware Volume。 |
| 45 | [Udf](../third_party/7zip/CPP/7zip/Archive/Udf/UdfHandler.cpp) | .udf、.iso、.img | 磁盘与分区 | 是 | — | 读取 UDF 光盘文件系统镜像。 |
| 46 | [VDI](../third_party/7zip/CPP/7zip/Archive/VdiHandler.cpp) | .vdi | 磁盘与分区 | 是 | — | 读取 VirtualBox 虚拟磁盘容器。 |
| 47 | [VHD](../third_party/7zip/CPP/7zip/Archive/VhdHandler.cpp) | .vhd | 磁盘与分区 | 是 | — | 读取 VHD 虚拟磁盘容器。 |
| 48 | [VHDX](../third_party/7zip/CPP/7zip/Archive/VhdxHandler.cpp) | .vhdx、.avhdx | 磁盘与分区 | 是 | — | 读取 VHDX 系列虚拟磁盘容器。 |
| 49 | [VMDK](../third_party/7zip/CPP/7zip/Archive/VmdkHandler.cpp) | .vmdk | 磁盘与分区 | 是 | — | 读取 VMDK 虚拟磁盘容器。 |
| 50 | [Xar](../third_party/7zip/CPP/7zip/Archive/XarHandler.cpp) | .xar、.pkg、.xip | 归档与软件包 | 是 | — | 读取 XAR 容器；PKG/XIP 为扩展别名，内部条目另按实际编码解析。 |
| 51 | [Z](../third_party/7zip/CPP/7zip/Archive/ZHandler.cpp) | .z、.taz | 压缩与编码流 | 是 | — | 读取 Unix compress 流；.taz 解码后的建议后缀为 .tar。 |
| 52 | [bzip2](../third_party/7zip/CPP/7zip/Archive/Bz2Handler.cpp) | .bz2、.bzip2、.tbz2、.tbz | 压缩与编码流 | 是 | 创建/条件更新 | 可创建单一 BZip2 流；多个文件先组成 TAR。 |
| 53 | [gzip](../third_party/7zip/CPP/7zip/Archive/GzHandler.cpp) | .gz、.gzip、.tgz、.tpz、.apk | 压缩与编码流 | 是 | 创建/条件更新 | 可创建单一 GZip 流；.tgz/.tpz 等别名含内层 .tar 提示；.apk 后缀也被 ZIP 使用。 |
| 54 | [lzma](../third_party/7zip/CPP/7zip/Archive/LzmaHandler.cpp) | .lzma | 压缩与编码流 | 是 | — | 读取独立 .lzma 流；默认完整 7zz 无此格式写入 handler。 |
| 55 | [lzma86](../third_party/7zip/CPP/7zip/Archive/LzmaHandler.cpp) | .lzma86 | 压缩与编码流 | 是 | — | 读取带 x86 分支转换信息的 LZMA86 流。 |
| 56 | [tar](../third_party/7zip/CPP/7zip/Archive/Tar/TarRegister.cpp) | .tar、.ova | 归档与软件包 | 是 | 创建/条件更新 | 可创建和更新 TAR，GNU/POSIX 是头格式选择；自身不压缩。 |
| 57 | [wim](../third_party/7zip/CPP/7zip/Archive/Wim/WimRegister.cpp) | .wim、.swm、.esd、.ppkg | 磁盘与分区 | 是 | 创建/条件更新 | 新建仅 Store；可读取压缩 WIM/ESD，更新须符合具体镜像条件。 |
| 58 | [xz](../third_party/7zip/CPP/7zip/Archive/XzHandler.cpp) | .xz、.txz | 压缩与编码流 | 是 | 创建/条件更新 | 可创建单一 XZ 流，使用 LZMA2；多个文件先组成 TAR。 |
| 59 | [zip](../third_party/7zip/CPP/7zip/Archive/Zip/ZipRegister.cpp) | .zip、.z01、.zipx、.jar、.xpi、.odt、.ods、.docx、.xlsx、.epub、.ipa、.apk、.appx | 归档与软件包 | 是 | 创建/条件更新 | 可创建与更新 ZIP；别名并非独立创建格式，签名 APK、分卷及带嵌入 stub 等实例可能只读。 |
| 60 | [zstd](../third_party/7zip/CPP/7zip/Archive/ZstdHandler.cpp) | .zst、.tzst | 压缩与编码流 | 是 | — | 默认 26.03 基线只读；.tzst 的内层建议后缀为 .tar。 |
| 61 | [Hash](../third_party/7zip/CPP/7zip/UI/Common/HashCalc.cpp) | .sha256、.sha512、.sha384、.sha224、.sha512-224、.sha512-256、.sha3-224、.sha3-256、.sha3-384、.sha3-512、.sha1、.sha2、.sha3、.sha、.md5、.blake2s、.blake2b、.blake2sp、.xxh64、.crc32、.crc64、.cksum、.asc | 校验摘要 | 是 | 生成清单 | 读取、生成和验证摘要清单；可计算算法取当前 10 个 hasher，后缀识别范围更广；不是承载文件内容的归档。 |

### 扩展别名、内容识别与更新边界

扩展名用于提示类型，最终由签名、handler 和实际打开结果确定。`.rar` 同时属于 Rar / Rar5，`.iso` 同时属于 Iso / Udf，`.img` 属于多类镜像与文件系统，`.swf` 同时属于 SWF / SWFc。关联设置按扩展名合并候选 handler，不用重复行冒充更多格式。

ZIP 注册的 JAR、XPI、ODT、ODS、DOCX、XLSX、EPUB、IPA、APK、APPX 是容器别名。能列出和提取这些包，不能直接推出“新建 Word 文档”“签名应用包”之类工作流。XAR 的 PKG/XIP、Compound 的 MSI/Office 文档同理。打开包后可以继续进入其可识别的嵌套容器。

读取可用与当前文件可修改分开显示：

- **ZIP**：`CInArchive::CanUpdate()` 会将多卷、APK、嵌入 stub、尾随数据、头错误和目录顺序不满足条件的实例判为不能更新。显示“ZIP：支持创建”，同时依据当前文件禁用“归档内删除/重命名/写回”。依据：[ZipIn.h](../third_party/7zip/CPP/7zip/Archive/Zip/ZipIn.h)、[ZipHandlerOut.cpp](../third_party/7zip/CPP/7zip/Archive/Zip/ZipHandlerOut.cpp)。
- **WIM**：`IsUpdateSupported()` 按版本、镜像数量、分卷结构、错误状态和固实资源决定可更新性。固实 ESD/WIM 等镜像可以读取，但不能据 `.wim` / `.esd` 后缀统一开放写回。新增数据编码采用 Copy。依据：[WimHandler.h](../third_party/7zip/CPP/7zip/Archive/Wim/WimHandler.h)、[WimHandlerOut.cpp](../third_party/7zip/CPP/7zip/Archive/Wim/WimHandlerOut.cpp)。
- **7z / TAR**：保留引擎对当前已打开文件的更新判断；固实归档修改可能需要重新压缩受影响块。可写格式不是任意损坏归档的修复承诺。依据：[7zHandlerOut.cpp](../third_party/7zip/CPP/7zip/Archive/7z/7zHandlerOut.cpp)、[TarHandlerOut.cpp](../third_party/7zip/CPP/7zip/Archive/Tar/TarHandlerOut.cpp)。
- **GZip / BZip2 / XZ**：输出接口接受一个文件流。BZip2 / GZip 要求单一条目；XZ 也允许创建空流。更新的含义是替换或复制这个流，不能作为多文件目录直接编辑。依据：[GzHandler.cpp](../third_party/7zip/CPP/7zip/Archive/GzHandler.cpp)、[Bz2Handler.cpp](../third_party/7zip/CPP/7zip/Archive/Bz2Handler.cpp)、[XzHandler.cpp](../third_party/7zip/CPP/7zip/Archive/XzHandler.cpp)。
- **Split / 分卷**：Split 是读取原始切片文件组的 handler；“分割文件”“合并文件”和“压缩为指定大小的分卷”是不同操作。分卷输出不增加归档格式数量。

### 默认 Zstd、RAR 与独立流能力

[ZstdHandler.cpp](../third_party/7zip/CPP/7zip/Archive/ZstdHandler.cpp) 有 `Z7_USE_ZSTD_COMPRESSION` 条件代码；该宏在本源码默认配置中未开启。[Arc_gcc.mak](../third_party/7zip/CPP/7zip/Bundles/Format7zF/Arc_gcc.mak) 链接 `ZstdDecoder.o` / `ZstdDec.o`，此源码副本没有条件分支引用的 `ZstdEncoder.h`、`ZstdEncoderProps.h`。默认注册的 `zstd` 输出工厂为 `NULL`。因此可读 `.zst` / `.tzst`，不可创建；不能仅打开一个宏就宣称本副本完成 Zstd 压缩迁移。

RAR / RAR5 注册项只有读取接口；Rar1、Rar2、Rar3、Rar5 四个 codec 都只有解码能力。原版 7-Zip 不创建 RAR，M7Arc 保留完整解压、测试与浏览能力。

LZMA、PPMd 编码器可服务 7z/ZIP，独立 `.lzma`、`.lzma86`、`.pmd` handler 在此完整引擎中的注册仍为只读。不能由“存在编码器”推导所有同名文件扩展都能创建。源码还含 AVB、LVM 等 handler 文件，但默认 `Arc_gcc.mak` 未加入对应对象；它们不计入当前 61 个可用项。

## 编码方法、预处理滤镜与参数

“归档格式”“压缩方法”“滤镜”“加密”“摘要算法”在模型和页面上使用独立字段。7z 是容器，LZMA2 是压缩方法，ARM64 是预处理滤镜，7zAES 是加密处理，SHA256 是摘要算法。

### 七类创建的参数映射

| 方法或容器 | 基础控件 | 高级参数与含义 | 原版 GUI 可见范围 |
|---|---|---|---|
| LZMA / LZMA2 | 压缩级别、字典、单词大小、线程 | `d` 字典、`fb` 快速匹配长度、`mf` 匹配器、`mc` 匹配循环、`lc/lp/pb` 上下文/位置位、`a` 算法模式、`mt` 线程、`zhb` 哈希位；LZMA `eos` 结束标志；LZMA2 `c` 分块大小 | 字典从 64 KiB 起；64 位 GUI 至 3.75 GiB，单词大小 8–273 的离散选项 |
| PPMd（7z） | 压缩级别、内存大小、模型阶数 | `mem` 模型内存、`o` 阶数；使用 PPMd7 | 64 位 GUI 内存 1 MiB–1 GiB 的离散选项；模型阶数 2–32 的离散选项 |
| PPMd（ZIP） | 压缩级别、内存大小、模型阶数 | `mem`、`o`，以及 PPMd8 的 `a` 恢复策略 | 内存 1–256 MiB，按 2 的幂选择；阶数 2–16 |
| Deflate / Deflate64 | 级别、固定字典、单词大小 | `fb`、`pass` 编码遍数、`mc`、`a` | 字典分别固定 32 / 64 KiB；单词大小上限分别 258 / 257 |
| BZip2 | 级别、块大小、线程 | `d` 块大小、`pass` 遍数、`mt` | GUI 显示 100–900 KiB；编码器按 100000 字节块单位归一化 |
| Copy | 存储级别 | 不执行压缩 | 7z / ZIP 由 0 级触发；WIM 使用此编码路径 |
| 7z 容器 | 固实块、文件名加密、滤镜 | `s` 固实分组、`he` 文件名/头加密、方法序列和流绑定；时间与文件元数据选项 | 固实可选自动、非固实、1 MiB–64 GiB 的 2 次幂块或完整固实；实际内存随方法变化 |
| ZIP 容器 | 加密、时间、字符编码 | ZipCrypto / AES 参数、时间记录和名称编码属性 | AES-256 与 ZipCrypto 两个原版加密选项；文件名加密不可用 |
| XZ 容器 | 流内块、字典、线程 | `s` 流内块、滤镜方法、`crc` 完整性检查 | 块选项沿用固实块控件；语义为单个流的内部块 |
| TAR | GNU / POSIX、时间和链接元数据 | `GNU/POSIX` 头格式与时间属性 | 不显示算法字典、加密或线程控件 |
| WIM | Store、文件树与镜像属性 | 镜像结构、时间和可表示的元数据 | 不把解码端 LZX / XPRESS / LZMS 放进创建方法下拉 |

参数解释以 [MethodProps.cpp](../third_party/7zip/CPP/7zip/Common/MethodProps.cpp) 与对应编码器 `SetCoderProperties` 为准。基础页呈现合法离散选项和默认值；高级页保留引擎参数输入与解析错误反馈。自动字典、线程和块大小由当前级别、方法、输入大小与可用内存共同确定，内存估算保留原版的压缩/解压两项。界面不为每种方法固定显示同一组参数。

### 25 个注册 codec

| Codec | 类型 | 编码 | 解码 |
|---|---|---|---|
| BCJ2 | 预处理/转换 | 是 | 是 |
| BCJ | 预处理/转换 | 是 | 是 |
| PPC | 预处理/转换 | 是 | 是 |
| IA64 | 预处理/转换 | 是 | 是 |
| ARM | 预处理/转换 | 是 | 是 |
| ARMT | 预处理/转换 | 是 | 是 |
| SPARC | 预处理/转换 | 是 | 是 |
| ARM64 | 预处理/转换 | 是 | 是 |
| RISCV | 预处理/转换 | 是 | 是 |
| Swap2 | 预处理/转换 | 是 | 是 |
| Swap4 | 预处理/转换 | 是 | 是 |
| BZip2 | 压缩 | 是 | 是 |
| Copy | 存储 | 是 | 是 |
| Deflate64 | 压缩 | 是 | 是 |
| Deflate | 压缩 | 是 | 是 |
| Delta | 预处理/转换 | 是 | 是 |
| LZMA2 | 压缩 | 是 | 是 |
| LZMA | 压缩 | 是 | 是 |
| PPMD | 压缩 | 是 | 是 |
| Rar1 | 压缩 | — | 是 |
| Rar2 | 压缩 | — | 是 |
| Rar3 | 压缩 | — | 是 |
| Rar5 | 压缩 | — | 是 |
| 7zAES | 加密 | 是 | 是 |
| AES256CBC | 加密 | 是 | 是 |

BCJ / BCJ2 处理 x86 分支地址；PPC、IA64、ARM、ARMT、SPARC、ARM64、RISCV 是架构分支转换；Delta 为差分转换；Swap2 / Swap4 调整字节顺序。BCJ2 使用多个输出流，注册记录中的流数为 4，因此它不是单流 XZ 滤镜。

XZ 可选 Delta、BCJ、PPC、IA64、ARM、ARMT、SPARC、ARM64、RISCV；它的压缩主体仍是 LZMA2。7z 能通过方法序列组合注册转换器与编码器。AES256CBC 是独立的注册加密转换器，不应在“压缩算法”中作为体积压缩方法展示。

注册 codec 表不穷尽所有容器内部解码实现。[Arc_gcc.mak](../third_party/7zip/CPP/7zip/Bundles/Format7zF/Arc_gcc.mak) 还链接 Implode、Shrink、LZFSE、LZH、LZMS、LZX、PPMdZip、Quantum、XPRESS、Zlib、Z、Zstd、XZ 等实现。其中 [ZIP 解码分派](../third_party/7zip/CPP/7zip/Archive/Zip/ZipHandler.cpp) 直接选择 Shrink、Implode、LZMA、XZ、PPMd、Zstd、LZFSE 等内部解码路径。格式中心区分“公开注册 codec”与“容器内部解码”，不将 25 误写成完整引擎仅有 25 项算法实现。

## 十种摘要算法与摘要文件

| 算法 | 摘要位数 |
|---|---|
| CRC32 | 32 |
| MD5 | 128 |
| SHA1 | 160 |
| SHA256 | 256 |
| SHA3-256 | 256 |
| SHA384 | 384 |
| SHA512 | 512 |
| XXH64 | 64 |
| CRC64 | 64 |
| BLAKE2sp | 256 |

Hash handler 由 [HashCalc.cpp](../third_party/7zip/CPP/7zip/UI/Common/HashCalc.cpp) 的 `Codecs_AddHashArcHandler()` 在公共 UI 层追加，不是 `Archive/` 目录里的普通压缩格式。它公开输入/输出接口，并带 `kHashHandler` 标识。

Hash 接受的 23 个扩展名范围大于当前十种可计算算法。例如 `.sha224`、`.sha512-224`、`.sha3-512`、`.blake2b` 可以触发摘要清单解析，但不因此获得相应算法实现。[Sha512Reg.cpp](../third_party/7zip/CPP/Common/Sha512Reg.cpp) 和 [Sha3Reg.cpp](../third_party/7zip/CPP/Common/Sha3Reg.cpp) 中部分变体注册处于注释状态。摘要页面只列当前 `GetHashMethods()` 返回的可计算算法；遇到清单要求未注册算法时，条目显示该算法不受支持，不能标为验证通过。

“CRC / SHA”页面提供计算所选文件/目录、复制结果、原版 SHA256/SHA1 摘要文件入口、打开摘要清单、测试清单对应的磁盘文件，以及逐文件失败结果。归档自带的 CRC 测试与用户生成摘要文件属于不同操作。

## 数据接口与实现落点

[原型格式数据](../design/prototype/formats.js) 暴露 `window.M7ArcFormats`：

| 字段 | 用途 |
|---|---|
| `formats` | 61 个 handler，含稳定 `id`、官方名称、扩展名、类别、读取/写入能力、行为说明、源文件 |
| `createFormats` | 七类真实创建格式，含引擎方法、原 GUI 方法、高级方法、级别、加密、固实、单流与线程属性 |
| `presets` | 三个 TAR 组合，每项记录 `pipeline`；不算新 handler |
| `codecs` | 25 个公开注册 codec 及编码/解码方向 |
| `hashers` | 十种实际注册的摘要算法及长度 |
| `methodDetails` | 字典/内存、单词大小/阶数、容器差异与高级参数映射 |
| `extensionMappings` | `.tgz → .tar` 等压缩流解码后的建议内层后缀 |

Qt 引擎适配层复用 [RegisterArc.h](../third_party/7zip/CPP/7zip/Common/RegisterArc.h)、[LoadCodecs.cpp](../third_party/7zip/CPP/7zip/UI/Common/LoadCodecs.cpp) 和 `CArcInfoEx` 的格式属性；`CreateOutArchive` / `UpdateEnabled` 决定格式级写入，打开后实例属性决定当前归档能否编辑。创建对话框参考 `CompressDialog.cpp`，条目级操作经 `IInArchive` / `IOutArchive` 及对应回调执行，Hash 继续使用公共 `HashCalc` 路径。

格式中心展示覆盖面；文件关联处理多个 handler 共用后缀；创建对话框只展示有效输出；属性面板展示当前实际格式与方法；只读归档在工具栏、菜单和拖入写回时使用同一实例能力判断。四个入口共用同一份能力数据。

## 已知边界

61 个 handler 表达官方基线实际注册能力。它不能覆盖任意私有变体、任意内部压缩方法、任意加密镜像或任意损坏数据。不能解码的条目按照引擎结果报告具体方法和状态。

macOS 的镜像浏览与提取不等同于修改磁盘或挂载卷；元数据跨平台写回由文件系统适配层处理。WIM 的 Windows 专属元数据与 macOS 的 ACL、扩展属性不能按同名字段直接等同。

Windows SFX 是归档加可执行 stub，不是新增 handler，也不能直接成为 macOS 原生可执行程序。M7Arc 的本机自解压产品形态需单独定义；现有 Windows SFX 的浏览和解压沿用当前读取能力。
