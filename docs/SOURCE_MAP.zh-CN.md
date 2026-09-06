# M7Arc 源码结构与引擎迁移

基线：7-Zip 26.03，提交 `0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`。
本文描述上游源码职责、M7Arc 的复用边界与引擎接入方式。上游路径均相对于 `third_party/7zip/`，`#L` 标注基线行号。

## 1. 源码整体结构

7-Zip 已包含官方 macOS 命令行实现。M7Arc 复用同一套压缩、加密和格式代码，将文件管理业务接入 Qt，并补齐 macOS 集成与文件元数据。

| 路径 | 上游职责 | M7Arc 处理方式 |
| --- | --- | --- |
| `DOC/` | 格式、算法、命令行、版本和许可证文档 | 保留，作为行为与分发依据 |
| `Asm/` | CRC、SHA、AES、LZMA 等 CPU 优化实现 | 沿用各架构官方选择 |
| `C/` | 编解码基础、stream、内存、线程、CPU 探测 | 直接复用 |
| `C/Util/7z` | C 语言 7z 解码示例 | 保留参考，不作为完整引擎 |
| `C/Util/Lzma`、`LzmaLib` | LZMA 示例与库 | 保留参考 |
| `C/Util/7zipInstall`、`7zipUninstall`、`SfxSetup` | Windows 安装、卸载与自解压工具 | 由 macOS 打包和 SFX 模块承接产品职责 |
| `CPP/Common/` | 容器、字符串、UTF 转换、COM 风格接口、通配符 | 复用，应用边界转换为 Qt/C++ 数据 |
| `CPP/Windows/` | Windows 公共实现及同文件中的 POSIX 分支 | 按文件选择；保留已实现的 POSIX 路径 |
| `CPP/7zip/Common/` | stream/filter、编码器创建、参数和注册 | 直接复用 |
| `CPP/7zip/Archive/` | archive、镜像、文件系统和可执行格式 handler | 完整复用 |
| `CPP/7zip/Compress/` | 编解码器与分支过滤器 | 完整复用 |
| `CPP/7zip/Crypto/` | 7z/ZIP/RAR 加密、密码派生和随机源 | 完整复用 |
| `CPP/7zip/UI/Common/` | 打开、枚举、提取、更新、哈希、benchmark | 保留非可视业务，适配 callback |
| `CPP/7zip/UI/Agent/` | archive 的文件夹视图、代理树、归档内操作 | 适配后进入同一 engine |
| `CPP/7zip/UI/FileManager/` | Windows 文件管理器和文件夹接口 | 保留接口语义，Qt 重写窗口与交互 |
| `CPP/7zip/UI/GUI/` | Windows 压缩、解压和进度对话框 | Qt 重写可视层 |
| `CPP/7zip/UI/Console/` | CLI 解析、输出和终端 callback | 保留官方 `7zz`；GUI 不解析其输出 |
| `CPP/7zip/UI/Explorer/` | Windows Shell 扩展 | 由 macOS 系统集成模块承接 |
| `CPP/7zip/UI/Far/` | FAR Manager 插件 | 保留上游代码，业务参考 Agent 接口 |
| `CPP/7zip/UI/Client7z/` | 共享模块调用示例 | 接口参考 |
| `CPP/7zip/Bundles/` | 不同功能组合的产物 | 以 Alone2 的组成为 engine 基础 |

目录说明见 [上游说明](../third_party/7zip/DOC/readme.txt)。`CPP/Windows` 的目录名不等于必须重新实现的代码范围。

## 2. Bundles 与产物

| Bundle | 产物/用途 | 迁移定位 |
| --- | --- | --- |
| `Alone2` | `7zz`，完整格式的独立 CLI | 核心对象与公共操作层基线 |
| `Alone` | `7za`，7z/xz/cab/zip/gzip/bzip2/tar 子集 | 精简构建参考 |
| `Alone7z` | `7zr`，精简 7z CLI | 精简构建参考 |
| `Fm` | `7zFM.exe`，包含格式实现的独立文件管理器 | 功能与 Agent 组合参考 |
| `Format7zF` | 全格式共享模块，Windows 为 `7z.dll` | `Arc_gcc.mak` 提供完整核心清单 |
| `Format7z` | `7za.dll`，7z 格式共享模块 | 单格式库参考 |
| `Format7zR` | `7zra.dll`，缩减 7z 模块 | 缩减库参考 |
| `Format7zExtract` | `7zxa.dll`，7z 解压模块 | 仅提取场景参考 |
| `Format7zExtractR` | 缩减 7z 解压模块 | 仅提取场景参考 |
| `LzmaCon` | `lzma`，LZMA 命令行工具 | 独立算法工具参考 |
| `SFXCon` | `7zCon`，控制台自解压模块 | macOS 终端 SFX 基础 |
| `SFXWin` | `7z.sfx`，Windows 图形自解压模块 | 提取流程参考 |
| `SFXSetup` | `7zS.sfx`，Windows 安装器自解压模块 | 安装流程参考 |

完整构建入口见 [Bundles/makefile](../third_party/7zip/CPP/7zip/Bundles/makefile#L1)。[Fm/makefile](../third_party/7zip/CPP/7zip/Bundles/Fm/makefile#L1) 将完整格式与 File Manager 组合。M7Arc 与官方 CLI 使用同一源码快照和格式清单；编译宏不同的目标分别构建对象。

## 3. macOS 构建与现有底层

[arm64 配置](../third_party/7zip/CPP/7zip/var_mac_arm64.mak#L1) 使用 Apple clang、`-arch arm64` 和 `USE_ASM=1`；[x86_64 配置](../third_party/7zip/CPP/7zip/var_mac_x64.mak#L1) 使用 clang、`-arch x86_64`，默认不启用 MASM 汇编。
两种架构分别构建，再组合 Universal 产物。M7Arc 保留该选择和各架构的 C/intrinsics 实现。

```sh
task_root="$PWD"
mkdir -p "$task_root/build/upstream-arm64" "$task_root/build/upstream-x64"
make -C third_party/7zip/CPP/7zip/Bundles/Alone2 -j4 -f ../../cmpl_mac_arm64.mak O="$task_root/build/upstream-arm64"
make -C third_party/7zip/CPP/7zip/Bundles/Alone2 -j4 -f ../../cmpl_mac_x64.mak O="$task_root/build/upstream-x64"
```

上述命令从 M7Arc 根目录执行。Apple Silicon 官方构建已通过；基础 7z/ZIP/TAR 的中文路径、空目录、可执行位与文件内容往返，以及加密文件名的 7z、分卷操作已有本机基线。
Intel 构建配置存在，Intel 运行结果仍需在对应环境确认。

| 底层职责 | 已有实现与源码 |
| --- | --- |
| 线程 | [C/Threads.c L419](../third_party/7zip/C/Threads.c#L419) 起为 pthread 路径 |
| Apple CPU 功能检测 | [C/CpuArch.c L793](../third_party/7zip/C/CpuArch.c#L793) 使用 Apple `sysctlbyname` 和架构判断 |
| LZMA 优化解码 | [LzmaDec_gcc.mak L1](../third_party/7zip/CPP/7zip/LzmaDec_gcc.mak#L1) 选择架构优化对象 |
| 文件枚举和属性 | [FileFind.cpp L1129](../third_party/7zip/CPP/Windows/FileFind.cpp#L1129) 处理 Darwin 时间、mode、uid/gid、inode |
| 时间恢复 | [FileDir.cpp L1191](../third_party/7zip/CPP/Windows/FileDir.cpp#L1191) 使用 POSIX 时间接口 |
| 权限恢复 | [FileIO.cpp L1082](../third_party/7zip/CPP/Windows/FileIO.cpp#L1082) 使用 `fchmod` |
| 符号链接 | [FileLink.cpp L682](../third_party/7zip/CPP/Windows/FileLink.cpp#L682) 使用 `symlink` |
| 共享模块 | [DLL.cpp L113](../third_party/7zip/CPP/Windows/DLL.cpp#L113) 使用 `dlopen/dlsym` |
| 随机源 | [RandGen.cpp L120](../third_party/7zip/CPP/7zip/Crypto/RandGen.cpp#L120) 使用 POSIX 随机源 |

## 4. M7Arc 的 engine 组成

采用 `Alone2` 的组合方式，完整保留 [Arc_gcc.mak](../third_party/7zip/CPP/7zip/Bundles/Format7zF/Arc_gcc.mak#L1) 的核心对象组：C、线程、Common、POSIX、7zip Common、Archive、Compress、Crypto。
[Alone2/makefile.gcc L55](../third_party/7zip/CPP/7zip/Bundles/Alone2/makefile.gcc#L55) 列出的公共操作对象提供文件扫描、打开、提取、更新和哈希；M7Arc 另接入适配后的 Agent 文件夹业务。

```text
Arc_gcc.mak 全部核心对象 + UI/Common 非可视业务 + Agent 适配
                              │
                  CMake OBJECT libraries
                              │
                   m7arc_engine SHARED
                              │
                 C++ ArchiveSession / Job API
                              │
                   Qt 控制器与模型快照
```

`m7arc_engine` 使用 `UI/Common/LoadCodecs.cpp` 作为唯一格式注册宿主，排除控制台入口和 `ArchiveExports.cpp`。
[LoadCodecs.cpp L116](../third_party/7zip/CPP/7zip/UI/Common/LoadCodecs.cpp#L116) 与 [ArchiveExports.cpp L18](../third_party/7zip/CPP/7zip/Archive/ArchiveExports.cpp#L18) 各自定义 `RegisterArc`，代表两种不同组合方式。
P0 使用完整内置格式目录；P5 在同一 LoadCodecs 宿主接通既有外部 codec/format 机制，内置格式保持完整。

注册依赖 [RegisterArc.h L47](../third_party/7zip/CPP/7zip/Common/RegisterArc.h#L47) 和 [RegisterCodec.h L31](../third_party/7zip/CPP/7zip/Common/RegisterCodec.h#L31) 的静态构造器。
OBJECT library 直接提供所有对象，避免普通静态库链接按引用取对象时遗漏仅靠注册生效的格式。
核心格式、codec 和 hasher 目录应与相同上游版本、相同构建选项的 `7zz i` 对应；缺项意味着对象清单或链接设置需要调整。

适配实现位于 M7Arc 自有目录，保留上游布局和少量明确补丁。Qt 直接调用共享引擎的 C++ API，应用与引擎使用同一工具链构建。
引擎库使用 macOS bundle 内明确的加载路径和 install name；官方 `Format7zF` 的非 Windows 默认名 `7z.so` 仅作为上游构建事实，见 [7zip_gcc.mak L99](../third_party/7zip/CPP/7zip/7zip_gcc.mak#L99)。

外部扩展沿用 [CreateCoder.h L13](../third_party/7zip/CPP/7zip/Common/CreateCoder.h#L13) 的内部/外部 codec 顺序及 [LoadCodecs.cpp L539](../third_party/7zip/CPP/7zip/UI/Common/LoadCodecs.cpp#L539) 的模块接口检查。相关对象统一启用 `Z7_EXTERNAL_CODECS`；外部 format 访问内建 codec 时还需接通 `EXPORT_CODECS` 和 `SetCodecs`。只适配 [LoadCodecs.cpp L850](../third_party/7zip/CPP/7zip/UI/Common/LoadCodecs.cpp#L850) 的路径入口：跳过独立主 `7z.so`，从应用明确传入的随包和用户扩展目录加载 Codecs/Formats。模块在启动加载、所有会话结束后释放，保留当前官方接口与架构要求。签名和第三方模块加载条件见总设计的引擎组成。

## 5. Agent 与公共操作层的分工

Agent 提供 archive 条目到文件夹树的映射，保留归档内路径、条目索引和写入语义；Qt 模型负责展示快照。
[AgentProxy.h L24](../third_party/7zip/CPP/7zip/UI/Agent/AgentProxy.h#L24) 的代理目录记录父子关系与 archive 索引，可支持 archive 中没有显式目录条目的情况。
[IFolderArchive.h L43](../third_party/7zip/CPP/7zip/UI/Agent/IFolderArchive.h#L43) 提供打开、重新打开和根文件夹绑定；[L65](../third_party/7zip/CPP/7zip/UI/Agent/IFolderArchive.h#L65) 提供删除和归档写操作。

| 接入部分 | M7Arc 适配 |
| --- | --- |
| `Agent.cpp`、`AgentProxy.*` | 保留代理和会话语义；格式对象生命周期归 engine 管理 |
| `ArchiveFolder.*` | 保留条目属性和目录导航；转为不可变模型快照 |
| `AgentOut.cpp`、`ArchiveFolderOut.cpp` | 保留归档内修改，接入统一任务与临时输出目录 |
| `UpdateCallbackAgent.*` | 转发进度、密码、文件错误和替换 archive 阶段 |
| `ArchiveFolderOpen.cpp` | 分离资源图标读取，格式图标由 Qt/macOS 层提供 |
| `FileManager/IFolder.h`、`Agent/IFolderArchive.h` | 作为引擎内部接口使用，不直接暴露给 QWidget/model |
| `UI/Common/WorkDir.*` | 工作目录设置来自任务选项，文件写入和最终替换流程保留 |

[Agent.cpp L23](../third_party/7zip/CPP/7zip/UI/Agent/Agent.cpp#L23) 引入注册表设置头；`Read_ShowDeleted` 位于 [L1664](../third_party/7zip/CPP/7zip/UI/Agent/Agent.cpp#L1664) 的注释块。适配移除无用注册表依赖，显示已删除条目作为显式会话选项。
[ArchiveFolderOpen.cpp L9](../third_party/7zip/CPP/7zip/UI/Agent/ArchiveFolderOpen.cpp#L9) 的资源字符串和 `HINSTANCE` 图标读取从非可视业务分离。
[WorkDir.cpp L61](../third_party/7zip/CPP/7zip/UI/Common/WorkDir.cpp#L61) 的配置加载改为输入参数，并保留临时文件到原 archive 的提交路径。

## 6. 接口映射

| M7Arc 操作 | 上游入口 | 应用侧职责 |
| --- | --- | --- |
| 格式目录 | [CArcInfoEx](../third_party/7zip/CPP/7zip/UI/Common/LoadCodecs.h#L96) | 展示能力、扩展名和格式属性 |
| 自动识别、嵌套打开、分卷 | [CArchiveLink](../third_party/7zip/CPP/7zip/UI/Common/OpenArchive.h#L390) | 会话、路径导航和用户询问 |
| 列举与属性 | [IInArchive](../third_party/7zip/CPP/7zip/Archive/IArchive.h#L316) | 将条目复制为模型快照 |
| 提取/测试 | [Extract](../third_party/7zip/CPP/7zip/UI/Common/Extract.h#L101) | 选择项、目标目录、覆盖策略 |
| 新建/更新 archive | [UpdateArchive](../third_party/7zip/CPP/7zip/UI/Common/Update.h#L210) | 参数、源文件、写入和完成状态 |
| 重命名规则 | [CRenamePair](../third_party/7zip/CPP/7zip/UI/Common/Update.h#L67) | 构造归档内旧/新路径 |
| 输出 archive | [IOutArchive](../third_party/7zip/CPP/7zip/Archive/IArchive.h#L530) | 由公共更新业务管理输出流 |
| 压缩参数 | [ISetProperties](../third_party/7zip/CPP/7zip/Archive/IArchive.h#L537) | 预设与高级属性转换 |
| 分卷写入 | [IArchiveUpdateCallback2](../third_party/7zip/CPP/7zip/Archive/IArchive.h#L453) | 分卷大小、输出路径和错误 |
| 密码 | [IPassword](../third_party/7zip/CPP/7zip/IPassword.h#L26) | 打开和更新的密码询问 |
| 进度/取消 | [IProgress](../third_party/7zip/CPP/7zip/IProgress.h#L12) | 任务状态、进度消息和取消 |
| 内存需求询问 | [IArchiveRequestMemoryUseCallback](../third_party/7zip/CPP/7zip/Archive/IArchive.h#L613) | 展示所需内存并接收决定 |
| 校验和 | [CHashBundle](../third_party/7zip/CPP/7zip/UI/Common/HashCalc.h#L81) | 用户发起的计算、查看和验证 |
| benchmark | [Bench.h](../third_party/7zip/CPP/7zip/UI/Common/Bench.h) | 运行配置与结果展示 |

格式可写性来自有效的 `CreateOutArchive`，见 [LoadCodecs.cpp L830](../third_party/7zip/CPP/7zip/UI/Common/LoadCodecs.cpp#L830)。打开后的只读状态及接口能力共同决定删除、重命名和更新操作是否可用。

## 7. 完整格式目录

本表对应 26.03 官方 macOS arm64 完整构建。扩展名为上游注册别名；同一扩展名可能由多个 handler 识别，打开时仍使用签名和上游识别规则。
“读”表示 handler 提供打开/解析及其支持的提取；“写”表示具备输出能力，具体方法和字段依格式而定。Hash 是校验和清单 handler。

| Handler | 注册扩展名 | 能力 |
| --- | --- | --- |
| 7z | 7z | 读写 |
| APFS | apfs img | 读 |
| APM | apm | 读 |
| Ar | ar a deb udeb lib | 读 |
| Arj | arj | 读 |
| Base64 | b64 | 读 |
| COFF | obj | 读 |
| Cab | cab | 读 |
| Chm | chm chi chq chw | 读 |
| Compound | msi msp msm doc xls ppt aaf | 读 |
| Cpio | cpio | 读 |
| CramFS | cramfs | 读 |
| Dmg | dmg | 读 |
| ELF | elf | 读 |
| Ext | ext ext2 ext3 ext4 img | 读 |
| FAT | fat img | 读 |
| FLV | flv | 读 |
| GPT | gpt mbr | 读 |
| HFS | hfs hfsx | 读 |
| Hxs | hxs hxi hxr hxq hxw lit | 读 |
| IHex | ihex | 读 |
| Iso | iso img | 读 |
| LP | lpimg img | 读 |
| Lzh | lzh lha | 读 |
| MBR | mbr | 读 |
| MachO | macho | 读 |
| MsLZ | mslz | 读 |
| Mub | mub | 读 |
| NTFS | ntfs img | 读 |
| Nsis | nsis | 读 |
| PE | exe dll sys | 读 |
| Ppmd | pmd | 读 |
| QCOW | qcow qcow2 qcow2c | 读 |
| Rar | rar r00 | 读 |
| Rar5 | rar r00 | 读 |
| Rpm | rpm | 读 |
| SWF | swf | 读 |
| SWFc | swf | 读 |
| Sparse | simg img | 读 |
| Split | 001 | 读 |
| SquashFS | squashfs | 读 |
| TE | te | 读 |
| UEFIc | scap | 读 |
| UEFIf | uefif | 读 |
| Udf | udf iso img | 读 |
| VDI | vdi | 读 |
| VHD | vhd | 读 |
| VHDX | vhdx avhdx | 读 |
| VMDK | vmdk | 读 |
| Xar | xar pkg xip | 读 |
| Z | z taz | 读 |
| bzip2 | bz2 bzip2 tbz2 tbz | 读写 |
| gzip | gz gzip tgz tpz apk | 读写 |
| lzma | lzma | 读 |
| lzma86 | lzma86 | 读 |
| tar | tar ova | 读写 |
| wim | wim swm esd ppkg | 读写 |
| xz | xz txz | 读写 |
| zip | zip z01 zipx jar xpi odt ods docx xlsx epub ipa apk appx | 读写 |
| zstd | zst tzst | 读 |
| Hash | sha256 sha512 sha384 sha224 sha512-224 sha512-256 sha3-224 sha3-256 sha3-384 sha3-512 sha1 sha2 sha3 sha md5 blake2s blake2b blake2sp xxh64 crc32 crc64 cksum asc | 读写清单 |

格式对象清单见 [Arc_gcc.mak L98](../third_party/7zip/CPP/7zip/Bundles/Format7zF/Arc_gcc.mak#L98)；ZIP 的别名和时间能力见 [ZipRegister.cpp L19](../third_party/7zip/CPP/7zip/Archive/Zip/ZipRegister.cpp#L19)。
默认可写 archive 大类为 7z、ZIP、TAR、GZIP、BZIP2、XZ、WIM。Hash 的清单生成单独作为文件校验功能。
GZIP/BZIP2/XZ 是单数据流；目录压缩为 `.tar.gz/.tar.bz2/.tar.xz` 时，业务层执行 TAR 与外层压缩两个阶段。

注册 codec 包含 BCJ2、BCJ、PPC、IA64、ARM、ARMT、SPARC、ARM64、RISCV、Swap2、Swap4、BZip2、Copy、Deflate64、Deflate、Delta、LZMA2、LZMA、PPMD、Rar1/2/3/5、7zAES、AES256CBC。
部分格式专用解码器直接由 handler 使用，未作为通用 codec 注册；完整对象清单必须同时保留两类。
本构建注册的 hasher 为 CRC32、MD5、SHA1、SHA256、SHA3-256、SHA384、SHA512、XXH64、CRC64、BLAKE2sp；Hash handler 的扩展名别名不等于每种算法都具备计算实现。

## 8. 线程、ABI 与生命周期

默认同时只执行一个引擎操作，包括打开、追加读取、压缩、提取、测试和校验。引擎内部的多线程压缩与解压保留。
Qt 主线程只操作会话描述和浏览快照；模型读取已完成的条目数据，不在 `data()` 或排序过程中同步调用 archive 对象。
`ArchiveSession` 持有 archive 和流，任务结束后再发布快照；关闭会话、更新完成后的重开及 codec 释放均在引擎执行线程顺序发生。

[IArchive.h L184](../third_party/7zip/CPP/7zip/Archive/IArchive.h#L184) 明确提取 callback 的文件操作函数互不并发，但进度 callback 可能与其并发。
进度转换为跨线程消息；密码、覆盖和内存询问由引擎任务等待 GUI 回答。取消唤醒等待并在 callback 返回 `E_ABORT`，暂停在 callback 的合作点执行。
[L214](../third_party/7zip/CPP/7zip/Archive/IArchive.h#L214) 说明进度字节可能代表压缩量或解压量；任务保存阶段、单位和总量是否已知。

`PROPVARIANT`、BSTR 和 COM 对象的初始化与释放遵循 [IArchive.h L20](../third_party/7zip/CPP/7zip/Archive/IArchive.h#L20)，适配使用 `CPropVariant`、`CMyComPtr`。
Qt 的 UTF-16 `QChar` 与 macOS `wchar_t` 不可直接重解释；`QString/UString/FString` 经明确转换，密码 BSTR 按 [IPassword.h L17](../third_party/7zip/CPP/7zip/IPassword.h#L17) 分配和释放。
当前接口使用上游 v23 之后的 `IUnknown` 约定，见 [MyWindows.h L147](../third_party/7zip/CPP/Common/MyWindows.h#L147)；应用与引擎共享当前头文件和构建设置。

[Wildcard.cpp L9](../third_party/7zip/CPP/Common/Wildcard.cpp#L9) 的 `g_CaseSensitive` 为进程全局状态，macOS 默认不区分大小写；[Agent.cpp L29](../third_party/7zip/CPP/7zip/UI/Agent/Agent.cpp#L29) 也持有全局 codec 生命周期。
单引擎操作队列避免任务之间同时修改这些状态。会话保留明确的 archive 名称策略，源卷和目标卷的大小写能力独立处理；处理大小写敏感卷与同名异大小写条目时不沿用单一 Mac 默认值。
需要并行执行多个引擎操作时，先将相关比较策略和生命周期准确下沉到操作/会话对象。

## 9. 现有能力边界

**Mac 元数据。** 上游 POSIX 路径已保存普通权限、链接和部分时间，但普通文件的 xattrs/resource fork 往返需要 M7Arc 的 `MacMetadata` 模块。
本机 7z/ZIP/TAR 默认往返没有保留人工设置的扩展属性；[UpdateCallback.cpp L514](../third_party/7zip/CPP/7zip/UI/Common/UpdateCallback.cpp#L514) 的源文件属性采集没有一般 Mac xattr 编组。
[FileFind.cpp L1147](../third_party/7zip/CPP/Windows/FileFind.cpp#L1147) 使用 ctime 而非 birth time；[FileDir.cpp L1228](../third_party/7zip/CPP/Windows/FileDir.cpp#L1228) 的 POSIX 恢复忽略 cTime。
`MacMetadata` 负责 Finder 标签、扩展属性、resource fork、birth time 与格式映射，ZIP 需要 AppleDouble 的采集/恢复。APFS/HFS 镜像内部资源流的读取与普通文件元数据往返分别实现。

**格式读写。** RAR/RAR5、DMG、APFS、HFS 和其余只读 handler 保留上游支持范围，浏览文件系统镜像不等于创建或回写卷。
[ZstdHandler.cpp L1124](../third_party/7zip/CPP/7zip/Archive/ZstdHandler.cpp#L1124) 在默认构建将输出工厂置空，完整对象清单只包含 Zstd decoder，条件引用的 encoder 文件未随本源码提供。
M7Arc 的默认 Zstd 能力为打开和解压。RAR 默认保留，分发遵守 [上游许可证](../third_party/7zip/DOC/License.txt) 与 [unRAR 许可](../third_party/7zip/DOC/unRarLicense.txt)。

**平台属性。** Windows ACL、NTFS alternate streams 和 POSIX/macOS 元数据具有不同语义。归档中已有属性可以展示；落盘恢复按目标文件系统支持的表达方式处理。
大多数 archive 更新会构建新的输出再替换，solid archive 的部分修改可能需要重压；任务应显示对应阶段和空间需求。

**SFX。** [SFXCon/makefile.gcc L57](../third_party/7zip/CPP/7zip/Bundles/SFXCon/makefile.gcc#L57) 已有非 Windows 构建，[SfxCon.cpp L118](../third_party/7zip/CPP/7zip/Bundles/SFXCon/SfxCon.cpp#L118) 支持提取、测试、密码与输出目录。
Windows SFX stub 生成的仍是 Windows 程序。Mac 双击自解压默认使用已签名且保持不变的提取器 `.app` 与同目录 `Payload.7z`，组合为 DMG；发布者可将 payload 放入 `Contents/Resources`，再对完整 app 签名和公证。
