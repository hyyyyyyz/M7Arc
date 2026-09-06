# M7Arc 功能迁移矩阵

M7Arc 以 7-Zip 的归档引擎为基础，提供面向 macOS 的 Qt 文件管理器。全功能目标覆盖原 File Manager、归档对话框、命令行的可移植能力，以及 macOS 对应的文件与系统操作。各格式的读写范围遵循实际 handler 能力；Windows 特有对象按本表提供 macOS 对应功能。

本表定义目标功能和交付阶段。当前交付为[交互原型](../design/m7arc-ui.html)与设计，Qt 应用按阶段实现。源码链接相对本文件所在的 `docs/` 目录，上游源树位于项目内 `third_party/7zip/`；链接后的数字为当前源码行号。`UI/` 指上游的 `CPP/7zip/UI/`。

[File Manager 规格](FILE_MANAGER_SPEC.zh-CN.md)逐项定义菜单、工具栏、面板与快捷键；[对话框与任务规格](DIALOG_SPEC.zh-CN.md)定义全部表单、提示与工具窗口；[格式与编码能力](FORMAT_CAPABILITIES.zh-CN.md)列出所有 61 个 handler、七类创建、三个 TAR 组合、25 个注册 codec 和十种 hasher。

## 阶段与能力边界

| 阶段 | 交付边界 |
| --- | --- |
| P0 | 建立 macOS 构建，保留 C/C++ 引擎，划清 Agent、Qt、平台代码边界，生成真实格式能力目录。 |
| P1 | 单面板详细信息视图、六菜单七按钮框架、内部打开、密码、属性、提取、测试、进度和错误处理。 |
| P2 | 七类真实创建与三个 TAR 组合预设，默认完整显示创建/更新参数，保留分卷、覆盖、原始参数和批量操作。 |
| P3 | 双面板文件管理、选择与排序、文件操作、外部编辑、嵌套归档回写。 |
| P4 | macOS 元数据、文件打开与关联、Services、Quick Look、Finder 拖放和系统位置。 |
| P5 | 哈希、基准、分割合并、全部工具、SFX 三类交付和高级命令行能力收齐。 |
| P6 | 双架构发布包、ad-hoc 签名、文档、本地化、安装与卸载体验；取得证书后增加 Developer ID 签名与公证。 |

`ArchiveSession` 持有归档层级和条目身份；`FolderModel` 负责 Qt 展示；`ArchiveJobs` 调用引擎并桥接进度；`TransferController` 处理文件与归档之间的操作；`MacPlatform` 承担系统 API。格式能力必须包含读取、创建、更新、加密、分卷等独立维度，不能由扩展名或一个“可写”开关推断全部操作。

## 主窗口与功能入口

首次打开为单面板详细信息视图，工作区由文件面板占满；F9 切换双面板，Tab 切换当前面板。属性、格式目录、设置和任务详情使用独立窗口。对应源码与全部命令 ID 见 [File Manager 规格](FILE_MANAGER_SPEC.zh-CN.md)。

| 结构 | 完整功能范围 | Qt 落点 | 阶段 |
| --- | --- | --- | --- |
| 六组菜单 | 文件、编辑、查看、收藏夹、工具、帮助；含原版动态打开/提取/添加/CRC 入口 | `CommandRegistry`、`QMenuBar`、`QAction` | P1–P5 |
| 七个工具栏动作 | 添加、提取、测试、复制、移动、删除、信息；按当前面板与选择能力启用 | 两组 `QToolBar`，与菜单复用动作 | P1–P3 |
| 面板导航 | 独立可编辑地址栏、上级、根、历史、归档层级；收藏夹十个固定槽 | `PanelController`、导航返回栈 | P1、P3 |
| 属性列与视图 | 全部普通/原始属性，列显隐/顺序/宽度，四种视图，五种排序，平面模式 | `QAbstractItemModel`、`QHeaderView`、排序代理 | P1、P3 |
| 四段状态栏 | 选择/总对象数、操作对象总大小、焦点大小、焦点修改时间 | 每个面板独立状态控件 | P1、P3 |
| 格式中心 | 61 个 handler（含 Hash）的分类/搜索/读写筛选，七类创建和三个组合，25 codec、10 hasher | 同一 `CCodecs` 目录与方法模型 | P0–P2、P5 |
| 完整添加窗口 | 格式、级别、方法、字典、单词大小/阶数、固实块、线程、压缩/解压内存、分卷、参数、更新/路径模式、操作选项、密码/加密默认可见 | `AddArchiveDialog` 两列参数布局 | P2 |
| 原版选项与工具窗口 | 六页设置、压缩附加选项、覆盖/密码/内存、进度/错误、基准、摘要、分割/合并、外部编辑保存 | 对话框与作业模型；见[字段规格](DIALOG_SPEC.zh-CN.md) | P1–P5 |

## 归档读取与写入

| 功能 | 源码依据 | M7Arc 落点与目标行为 | 阶段 |
| --- | --- | --- | --- |
| 7z 读取、创建、更新 | [7zRegister.cpp](../third_party/7zip/CPP/7zip/Archive/7z/7zRegister.cpp):14 | 保留原 handler；创建、加密、固实压缩和更新均由引擎处理。 | P1–P2 |
| ZIP 读取、创建、更新 | [ZipRegister.cpp](../third_party/7zip/CPP/7zip/Archive/Zip/ZipRegister.cpp):19 | `ArchiveJobs` 连接 ZIP handler；按方法和加密能力配置界面。 | P1–P2 |
| gzip 读取、创建 | [GzHandler.cpp](../third_party/7zip/CPP/7zip/Archive/GzHandler.cpp):1190 | 保留单数据流语义；压缩目录时提供先 TAR 后 gzip 的组合操作。 | P1–P2 |
| bzip2 读取、创建 | [Bz2Handler.cpp](../third_party/7zip/CPP/7zip/Archive/Bz2Handler.cpp):474 | 单流格式只开放适用操作；目录归档使用 TAR 组合。 | P1–P2 |
| xz 读取、创建 | [XzHandler.cpp](../third_party/7zip/CPP/7zip/Archive/XzHandler.cpp):1445 | 保留压缩方法、过滤器与完整性校验参数；支持 TAR 组合。 | P1–P2 |
| TAR 读取、创建、更新 | [TarRegister.cpp](../third_party/7zip/CPP/7zip/Archive/Tar/TarRegister.cpp):14 | 保留 TAR handler 和 POSIX 属性；作为多文件与单流压缩的组合容器。 | P1–P2 |
| WIM 读取、创建、条件更新 | [WimRegister.cpp](../third_party/7zip/CPP/7zip/Archive/Wim/WimRegister.cpp):12；[WimHandler.h](../third_party/7zip/CPP/7zip/Archive/Wim/WimHandler.h) | 新建仅 Store；读取压缩 WIM/ESD；当前实例按固实资源、版本、卷和镜像结构判定能否更新。Windows 安全描述符独立呈现。 | P1–P2 |
| zstd 读取 | [ZstdHandler.cpp](../third_party/7zip/CPP/7zip/Archive/ZstdHandler.cpp):6、1124 | 当前默认构建将 `CreateArcOut` 设为 NULL；`.zst/.tzst` 可读，源码副本没有条件压缩分支所引用的编码器文件。 | P1 |
| RAR 与 RAR5 读取 | [RarHandler.cpp](../third_party/7zip/CPP/7zip/Archive/Rar/RarHandler.cpp):1768；[Rar5Handler.cpp](../third_party/7zip/CPP/7zip/Archive/Rar/Rar5Handler.cpp):3399 | 保留解码、密码和分卷读取；不提供 RAR 创建或更新。 | P1 |
| DMG、HFS 读取 | [DmgHandler.cpp](../third_party/7zip/CPP/7zip/Archive/DmgHandler.cpp):2328；[HfsHandler.cpp](../third_party/7zip/CPP/7zip/Archive/HfsHandler.cpp):2624 | 作为可浏览容器读取与提取；镜像挂载是独立系统操作。 | P1 |
| ISO、UDF 读取 | [IsoRegister.cpp](../third_party/7zip/CPP/7zip/Archive/Iso/IsoRegister.cpp):14；[UdfHandler.cpp](../third_party/7zip/CPP/7zip/Archive/Udf/UdfHandler.cpp):419 | 在相同目录模型中显示镜像内容和属性。 | P1 |
| FAT、NTFS、ext、SquashFS 读取 | [FatHandler.cpp](../third_party/7zip/CPP/7zip/Archive/FatHandler.cpp):1318；[NtfsHandler.cpp](../third_party/7zip/CPP/7zip/Archive/NtfsHandler.cpp):2896；[ExtHandler.cpp](../third_party/7zip/CPP/7zip/Archive/ExtHandler.cpp):2853；[SquashfsHandler.cpp](../third_party/7zip/CPP/7zip/Archive/SquashfsHandler.cpp):2366 | 保留镜像解析；浏览与提取不等于向目标卷恢复原文件系统全部语义。 | P1 |
| VHD/VHDX、VDI、VMDK、QCOW 读取 | [VhdHandler.cpp](../third_party/7zip/CPP/7zip/Archive/VhdHandler.cpp):960；[VhdxHandler.cpp](../third_party/7zip/CPP/7zip/Archive/VhdxHandler.cpp):2078；[VdiHandler.cpp](../third_party/7zip/CPP/7zip/Archive/VdiHandler.cpp):431；[VmdkHandler.cpp](../third_party/7zip/CPP/7zip/Archive/VmdkHandler.cpp):1523；[QcowHandler.cpp](../third_party/7zip/CPP/7zip/Archive/QcowHandler.cpp):706 | 保留虚拟磁盘解析和多层容器导航。 | P1 |
| CAB、CHM、ARJ、LZH、CPIO、RPM、XAR、Z 等读取 | [CabRegister.cpp](../third_party/7zip/CPP/7zip/Archive/Cab/CabRegister.cpp):12；[ChmHandler.cpp](../third_party/7zip/CPP/7zip/Archive/Chm/ChmHandler.cpp):766；[ArjHandler.cpp](../third_party/7zip/CPP/7zip/Archive/ArjHandler.cpp):970；[CpioHandler.cpp](../third_party/7zip/CPP/7zip/Archive/CpioHandler.cpp):1103；[XarHandler.cpp](../third_party/7zip/CPP/7zip/Archive/XarHandler.cpp):1309 | 构建完整格式注册集合；不以常见格式白名单缩减上游能力。 | P0–P1 |
| PE、ELF、Mach-O、UEFI、分区等容器读取 | [PeHandler.cpp](../third_party/7zip/CPP/7zip/Archive/PeHandler.cpp):2997；[ElfHandler.cpp](../third_party/7zip/CPP/7zip/Archive/ElfHandler.cpp):1277；[MachoHandler.cpp](../third_party/7zip/CPP/7zip/Archive/MachoHandler.cpp):736；[UefiHandler.cpp](../third_party/7zip/CPP/7zip/Archive/UefiHandler.cpp):1853；[ApmHandler.cpp](../third_party/7zip/CPP/7zip/Archive/ApmHandler.cpp):416 | 高级“内部打开”保留解析器能力；可执行文件内容浏览与运行分开操作。 | P1 |
| APFS、CramFS 读取 | [ApfsHandler.cpp](../third_party/7zip/CPP/7zip/Archive/ApfsHandler.cpp)；[CramfsHandler.cpp](../third_party/7zip/CPP/7zip/Archive/CramfsHandler.cpp) | 完整文件系统镜像读取集合；内层目录采用同一面板。 | P1 |
| APM、GPT、MBR、LP、Sparse 读取 | [完整 handler 目录](FORMAT_CAPABILITIES.zh-CN.md) | 读取分区表、Android 逻辑分区与 sparse 镜像；继续进入可识别的内层流和文件系统。 | P1 |
| Ar、Hxs、Compound 读取 | [完整 handler 目录](FORMAT_CAPABILITIES.zh-CN.md) | 保留静态库、Debian 包、Help 2、OLE/MSI/Office 容器及全部扩展别名。 | P1 |
| Base64、IHex、MsLZ、lzma、lzma86、Ppmd 读取 | [完整 handler 目录](FORMAT_CAPABILITIES.zh-CN.md) | 使用对应流 handler；同名算法的编码能力不推导独立流创建能力。 | P1 |
| COFF、TE、Mub、FLV、SWF、SWFc 读取 | [完整 handler 目录](FORMAT_CAPABILITIES.zh-CN.md) | 保留目标文件、Universal Binary、媒体容器与压缩 SWF 的内部浏览/提取。 | P1 |
| Hash 清单读取、生成与验证 | [HashCalc.cpp](../third_party/7zip/CPP/7zip/UI/Common/HashCalc.cpp) | 作为第 61 个 handler 单独识别，计算使用十种实际注册 hasher；摘要别名不增加可计算算法。 | P1、P5 |
| TAR + GZip / BZip2 / XZ | [UpdateGUI.cpp](../third_party/7zip/CPP/7zip/UI/GUI/UpdateGUI.cpp)；[组合定义](FORMAT_CAPABILITIES.zh-CN.md) | 创建窗口提供 `.tar.gz`、`.tar.bz2`、`.tar.xz` 三个预设；先 TAR 后流编码，多文件/目录可用，更新时重建外层。 | P2 |
| 自动识别与指定打开格式 | [Agent.cpp](../third_party/7zip/CPP/7zip/UI/Agent/Agent.cpp):1627、1660 | `ArchiveSession` 通过原 `Open`/`ParseOpenTypes` 打开，不仅按文件后缀判断。 | P1 |
| 普通内部打开、`*`、`#` parser | [resource.rc](../third_party/7zip/CPP/7zip/UI/FileManager/resource.rc):37–41 | 普通打开保留主要入口；特殊模式放入“作为归档打开”菜单。 | P1 |
| 归档目录树与虚拟目录 | [IFolder.h](../third_party/7zip/CPP/7zip/UI/FileManager/IFolder.h):29；[AgentProxy.cpp](../third_party/7zip/CPP/7zip/UI/Agent/AgentProxy.cpp):344、912 | `FolderModel` 适配既有目录代理，保留真实 archive index。 | P1 |
| 条目与多层归档属性 | [IFolder.h](../third_party/7zip/CPP/7zip/UI/FileManager/IFolder.h):124、130 | 信息/属性窗口完整显示当前条目、当前文件夹和各容器层原始属性。 | P1 |
| 连续分卷与分割流读取 | [SplitHandler.cpp](../third_party/7zip/CPP/7zip/Archive/SplitHandler.cpp):347；[IFolderArchive.h](../third_party/7zip/CPP/7zip/UI/Agent/IFolderArchive.h):43 | 复用原打开 callback；缺失下一卷时请求路径或显示明确错误。 | P1 |
| 嵌套归档读取 | [PanelItemOpen.cpp](../third_party/7zip/CPP/7zip/UI/FileManager/PanelItemOpen.cpp):456、1526、1670 | 优先 seekable stream，其次内存或临时磁盘；每层有独立 session 记录。 | P1 |
| 创建、删除、重命名、注释、单文件回写 | [AgentOut.cpp](../third_party/7zip/CPP/7zip/UI/Agent/AgentOut.cpp):482、537、592、661、708 | 复用更新算法；依格式及当前归档实例开放操作。 | P2–P3 |
| 临时新归档、提交与重开 | [ArchiveFolderOut.cpp](../third_party/7zip/CPP/7zip/UI/Agent/ArchiveFolderOut.cpp):125、183、255 | 写入成功后替换并重开，恢复目录；失败保留可用原档或明确可恢复文件。 | P2 |
| 当前实例的只读能力 | [Agent.h](../third_party/7zip/CPP/7zip/UI/Agent/Agent.h):263；[Agent.cpp](../third_party/7zip/CPP/7zip/UI/Agent/Agent.cpp):1611 | UI 跟随 `UpdateEnabled`、归档层级、设备与 tail 等实际约束禁用修改。 | P0–P1 |

当前完整基线包含 **61 个 handler（含 Hash）、七类真实创建格式、三个 TAR 组合预设、25 个注册 codec、十种 hasher**。七类创建为 **7z、ZIP、GZip、BZip2、XZ、TAR、WIM**；逐条目增删、加密、固实和分卷更新按格式与实例分别判断。原版普通添加窗口对目录/多文件排除 GZip、BZip2、XZ，M7Arc 通过三个组合预设提供对应工作流。

## 压缩、解压与作业参数

| 功能 | 源码依据 | M7Arc 落点与目标行为 | 阶段 |
| --- | --- | --- | --- |
| 目标归档路径、文件名、格式 | [CompressDialog.rc](../third_party/7zip/CPP/7zip/UI/GUI/CompressDialog.rc):61–67 | `CompressDialog` 使用 macOS 文件选择器和格式联动名称。 | P2 |
| 压缩级别、方法 | [CompressDialog.rc](../third_party/7zip/CPP/7zip/UI/GUI/CompressDialog.rc):69–73 | 默认完整显示格式级别：7z 0–9、ZIP 0/1/3/5/7/9、GZip 1/5/7/9、BZip2 1/3/5/7/9、XZ 1–9、TAR/WIM 0；方法按对应表生成。 | P2 |
| 普通方法与扩展方法 | [CompressDialog.cpp](../third_party/7zip/CPP/7zip/UI/GUI/CompressDialog.cpp) 的 `SetMethod2` | 7z 普通下拉为 LZMA2/LZMA/PPMd/BZip2，扩展项为 Deflate/Deflate64；Copy 由 0 级触发。ZIP 保留五种普通方法；TAR GNU/POSIX 表示头格式，WIM Store。 | P2 |
| 字典、word size、solid block | [CompressDialog.rc](../third_party/7zip/CPP/7zip/UI/GUI/CompressDialog.rc):75–87 | 原添加窗口中的字典/内存、单词大小/模型阶数、固实块默认可见，随方法与容器变化；参数变化同步资源估计。 | P2 |
| CPU 线程、压缩内存限额 | [CompressDialog.rc](../third_party/7zip/CPP/7zip/UI/GUI/CompressDialog.rc):89–96 | 默认显示引擎线程参数、硬件线程数与压缩内存上限；保留 codec 的线程语义。 | P2 |
| 解压内存估计与全局限额 | [CompressDialog.rc](../third_party/7zip/CPP/7zip/UI/GUI/CompressDialog.rc):98；[SettingsPage2.rc](../third_party/7zip/CPP/7zip/UI/FileManager/SettingsPage2.rc):21 | 展示预估解压内存；超限交给作业交互处理。 | P1–P2 |
| 分卷尺寸与尺寸序列 | [CompressDialog.rc](../third_party/7zip/CPP/7zip/UI/GUI/CompressDialog.rc):102；[ArchiveCommandLine.cpp](../third_party/7zip/CPP/7zip/UI/Common/ArchiveCommandLine.cpp):953 | 保留常用尺寸、自定义尺寸和引擎支持的卷尺寸序列。 | P2 |
| 原始方法参数 | [CompressDialog.rc](../third_party/7zip/CPP/7zip/UI/GUI/CompressDialog.rc):105；[ArchiveCommandLine.cpp](../third_party/7zip/CPP/7zip/UI/Common/ArchiveCommandLine.cpp):1005 | 原始参数栏默认可见，直接映射 `CProperty`；与表单合成最终有效配置并呈现解析错误。 | P2 |
| 添加、更新、仅刷新、同步 | [CompressDialog.cpp](../third_party/7zip/CPP/7zip/UI/GUI/CompressDialog.cpp):370；[UpdateAction.cpp](../third_party/7zip/CPP/7zip/UI/Common/UpdateAction.cpp):9、20、31、42 | 复用四组更新规则；同步需展示其会删除归档中缺失源项的语义。 | P2 |
| 压缩路径模式 | [CompressDialog.rc](../third_party/7zip/CPP/7zip/UI/GUI/CompressDialog.rc):115；[Update.h](../third_party/7zip/CPP/7zip/UI/Common/Update.h):107 | 参数模型保留相对/完整等引擎路径模式，GUI 使用可理解的示例。 | P2 |
| 压缩共享文件、遇打开错误停止 | [CompressDialog.rc](../third_party/7zip/CPP/7zip/UI/GUI/CompressDialog.rc):123；[Update.h](../third_party/7zip/CPP/7zip/UI/Common/Update.h):85 | 复用源文件读取策略；macOS 按本地文件 API 实际语义呈现。 | P2 |
| 压缩后删除源文件 | [CompressDialog.rc](../third_party/7zip/CPP/7zip/UI/GUI/CompressDialog.rc):125；[ArchiveFolderOut.cpp](../third_party/7zip/CPP/7zip/UI/Agent/ArchiveFolderOut.cpp):237 | 仅在归档提交成功后删除已处理源文件。 | P2 |
| 密码、确认密码、显示密码 | [CompressDialog.rc](../third_party/7zip/CPP/7zip/UI/GUI/CompressDialog.rc):131–137 | 原生 Qt 密码输入；密码传递给引擎 callback，不拼进子进程命令。 | P1–P2 |
| 加密方法、加密文件名 | [CompressDialog.rc](../third_party/7zip/CPP/7zip/UI/GUI/CompressDialog.rc):139–143 | 依格式开放；不能将仅内容加密展示为名称也被隐藏。 | P2 |
| 符号链接、硬链接 | [CompressOptionsDialog.rc](../third_party/7zip/CPP/7zip/UI/GUI/CompressOptionsDialog.rc):26–29；[Update.h](../third_party/7zip/CPP/7zip/UI/Common/Update.h):101 | 保留链接存储选项；macOS 用 POSIX 链接，按格式支持恢复。 | P2、P4 |
| Windows streams/security | [CompressOptionsDialog.rc](../third_party/7zip/CPP/7zip/UI/GUI/CompressOptionsDialog.rc):30–33 | 保留归档内属性读取；本地 macOS 存储方案见系统集成表。 | P4 |
| 时间精度、mtime、creation、atime | [CompressOptionsDialog.rc](../third_party/7zip/CPP/7zip/UI/GUI/CompressOptionsDialog.rc):43–60 | 按格式保存；macOS birth time 与 POSIX ctime 分开建模。 | P2、P4 |
| 归档时间取最新文件时间、保留源 atime | [CompressOptionsDialog.rc](../third_party/7zip/CPP/7zip/UI/GUI/CompressOptionsDialog.rc):65–73 | 保留参数及提交后应用时机。 | P2 |
| 提取目录、目标子目录开关 | [ExtractDialog.rc](../third_party/7zip/CPP/7zip/UI/GUI/ExtractDialog.rc):31–36 | `ExtractDialog` 展示最终输出目录，可直接提取到指定位置。 | P1 |
| 提取完整路径、当前路径、无路径、绝对路径 | [ExtractMode.h](../third_party/7zip/CPP/7zip/UI/Common/ExtractMode.h):8 | 普通 UI 提供常用模式；完整引擎模式保留在高级选项与 CLI。 | P1、P5 |
| 消除重复根目录 | [ExtractDialog.rc](../third_party/7zip/CPP/7zip/UI/GUI/ExtractDialog.rc):41 | 保留 `ElimDup` 语义，不凭文件名另写根目录猜测算法。 | P1 |
| 询问、覆盖、跳过、改新文件名、改旧文件名 | [ExtractMode.h](../third_party/7zip/CPP/7zip/UI/Common/ExtractMode.h):20 | 冲突 sheet 支持单次/全部、原文件与新文件大小和时间比较。 | P1 |
| 覆盖确认与取消 | [OverwriteDialog.rc](../third_party/7zip/CPP/7zip/UI/FileManager/OverwriteDialog.rc):24–42 | 作业等待 UI 决策，取消只终止当前作业，准确保留已完成状态。 | P1 |
| 解压密码、错误密码后重试 | [ExtractDialog.rc](../third_party/7zip/CPP/7zip/UI/GUI/ExtractDialog.rc):48–50；[IFolderArchive.h](../third_party/7zip/CPP/7zip/UI/Agent/IFolderArchive.h):43 | 逐归档 session 管理密码交互，支持嵌套层使用不同密码。 | P1 |
| 测试归档、批量解压、选中条目提取 | [Extract.h](../third_party/7zip/CPP/7zip/UI/Common/Extract.h):57、101；[IFolderArchive.h](../third_party/7zip/CPP/7zip/UI/Agent/IFolderArchive.h):26 | 统一作业入口，测试模式不创建普通输出文件。 | P1 |
| 后台、暂停/继续、取消 | [ProgressDialog2a.rc](../third_party/7zip/CPP/7zip/UI/FileManager/ProgressDialog2a.rc):46；[ProgressDialog2.cpp](../third_party/7zip/CPP/7zip/UI/FileManager/ProgressDialog2.cpp):100、1150 | 作业控制桥接引擎 callback；macOS 后台策略与 UI 可见性分别处理。 | P1、P4 |
| 时间、速度、文件数、错误、大小、压缩比 | [ProgressDialog2a.rc](../third_party/7zip/CPP/7zip/UI/FileManager/ProgressDialog2a.rc):51–84 | `JobModel` 汇总扫描、读取、处理、提交各阶段及逐文件错误。 | P1–P2 |
| 提交阶段取消边界 | [ArchiveFolderOut.cpp](../third_party/7zip/CPP/7zip/UI/Agent/ArchiveFolderOut.cpp):74–82、226–231 | 替换中进入不可中断收尾；完成提交与重开后返回实际结果。 | P2 |

## 文件管理与嵌套编辑

| 功能 | 源码依据 | M7Arc 落点与目标行为 | 阶段 |
| --- | --- | --- | --- |
| 单/双面板、焦点切换 | [App.h](../third_party/7zip/CPP/7zip/UI/FileManager/App.h):17、64、73；[resource.rc](../third_party/7zip/CPP/7zip/UI/FileManager/resource.rc):109 | `MainWindow` 默认一个详细信息面板，F9 切换两个独立 `PanelController`，Tab 切换焦点；保留双方状态。 | P1、P3 |
| 面板路径同步、另一面板进入子目录 | [App.h](../third_party/7zip/CPP/7zip/UI/FileManager/App.h):104–106 | 面板命令直接操作目标 session，不共享可变浏览状态。 | P3 |
| 地址栏、上级、根、历史、收藏 | [resource.rc](../third_party/7zip/CPP/7zip/UI/FileManager/resource.rc):124–140；[PanelFolderChange.cpp](../third_party/7zip/CPP/7zip/UI/FileManager/PanelFolderChange.cpp):335、866 | 地址栏显示本地路径和归档层级；收藏及历史持久化。 | P1、P3 |
| 打开、内部打开、外部打开 | [resource.rc](../third_party/7zip/CPP/7zip/UI/FileManager/resource.rc):37–41 | 内部打开交给引擎，外部打开通过 `MacPlatform` 交给默认应用。 | P1、P4 |
| 大图标、小图标、列表、详细信息 | [resource.rc](../third_party/7zip/CPP/7zip/UI/FileManager/resource.rc):97–100 | Qt item views 共用同一 model 与选择状态。 | P3 |
| 名称、类型、时间、大小排序、不排序 | [resource.rc](../third_party/7zip/CPP/7zip/UI/FileManager/resource.rc):102–106 | `FolderModel`/proxy 排序保留未排序的原 archive 顺序。 | P3 |
| 自定义属性列、时间显示精度 | [Panel.h](../third_party/7zip/CPP/7zip/UI/FileManager/Panel.h):87；[App.h](../third_party/7zip/CPP/7zip/UI/FileManager/App.h):199 | 按目录能力生成列并保存顺序、宽度和显示设置。 | P3 |
| 平铺视图 | [IFolder.h](../third_party/7zip/CPP/7zip/UI/FileManager/IFolder.h):114；[resource.rc](../third_party/7zip/CPP/7zip/UI/FileManager/resource.rc):108 | 保留代理的 flat mode；显示相对目录避免仅显示同名文件。 | P3 |
| 全选、取消、反选 | [resource.rc](../third_party/7zip/CPP/7zip/UI/FileManager/resource.rc):86–88 | 使用 Qt selection model，支持原有键盘工作流和 macOS 快捷键。 | P3 |
| 按掩码、按类型选择和取消 | [resource.rc](../third_party/7zip/CPP/7zip/UI/FileManager/resource.rc):89–93 | 复用 wildcard 语义；选中结果与显示过滤分开。 | P3 |
| 刷新、自动刷新 | [resource.rc](../third_party/7zip/CPP/7zip/UI/FileManager/resource.rc):127–128 | 本地目录监听与主动刷新结合；归档更新成功后重新读取。 | P1、P3 |
| 重命名、新建目录、新建文件 | [resource.rc](../third_party/7zip/CPP/7zip/UI/FileManager/resource.rc):45、72–73；[IFolder.h](../third_party/7zip/CPP/7zip/UI/FileManager/IFolder.h):76 | 本地 provider 与归档 operations 使用同一命令入口，按能力启用。 | P3 |
| 本地→本地复制/移动 | [App.cpp](../third_party/7zip/CPP/7zip/UI/FileManager/App.cpp):565、788 | `TransferController` 保留进度、冲突、取消及 macOS 文件属性。 | P3–P4 |
| 本地→归档添加/移动 | [App.cpp](../third_party/7zip/CPP/7zip/UI/FileManager/App.cpp):801；[ArchiveFolderOut.cpp](../third_party/7zip/CPP/7zip/UI/Agent/ArchiveFolderOut.cpp):377 | 添加调用 `CopyFrom`；移动在成功提交后删除源文件。 | P2–P3 |
| 归档→本地复制/提取 | [ArchiveFolder.cpp](../third_party/7zip/CPP/7zip/UI/Agent/ArchiveFolder.cpp):32 | 调用归档提取路径，保留相对路径与平铺模式语义。 | P1、P3 |
| 归档→归档复制 | [App.cpp](../third_party/7zip/CPP/7zip/UI/FileManager/App.cpp):760–824 | 通过临时目录提取后加入目标归档，目标成功前保持源不变。 | P3 |
| 归档→本地/另一归档移动 | [ArchiveFolder.cpp](../third_party/7zip/CPP/7zip/UI/Agent/ArchiveFolder.cpp):36 | 新增“完成目标写入后删除源条目”流程，失败时保留源；原实现限制见末节。 | P3 |
| 删除、移到废纸篓 | [PanelOperations.cpp](../third_party/7zip/CPP/7zip/UI/FileManager/PanelOperations.cpp):112 | 本地文件使用 macOS 废纸篓；归档删除走更新事务并明确不同语义。 | P3–P4 |
| 属性、注释 | [resource.rc](../third_party/7zip/CPP/7zip/UI/FileManager/resource.rc):53–54；[AgentOut.cpp](../third_party/7zip/CPP/7zip/UI/Agent/AgentOut.cpp):661 | 属性只读展示全面；仅对支持注释的对象提供修改。 | P1、P3 |
| 外部查看器、编辑器、diff | [EditPage2.rc](../third_party/7zip/CPP/7zip/UI/FileManager/EditPage2.rc):3–13；[PanelItemOpen.cpp](../third_party/7zip/CPP/7zip/UI/FileManager/PanelItemOpen.cpp):719、747 | 配置应用或程序参数；归档条目按需提取；不将外部查看误作内置编辑器。 | P3 |
| 包内文件编辑回写 | [PanelItemOpen.cpp](../third_party/7zip/CPP/7zip/UI/FileManager/PanelItemOpen.cpp):1032、1749 | `EditSession` 绑定原归档和条目，提供更新、另存、放弃。 | P3 |
| 嵌套归档逐级回写 | [PanelItemOpen.cpp](../third_party/7zip/CPP/7zip/UI/FileManager/PanelItemOpen.cpp):598；[PanelFolderChange.cpp](../third_party/7zip/CPP/7zip/UI/FileManager/PanelFolderChange.cpp):999 | 内层提交后向外层传播；保存、放弃、取消分别处理。 | P3 |
| 临时目录策略、清理临时文件 | [FoldersPage2.rc](../third_party/7zip/CPP/7zip/UI/FileManager/FoldersPage2.rc):4–10；[PanelItemOpen.cpp](../third_party/7zip/CPP/7zip/UI/FileManager/PanelItemOpen.cpp):1815 | 系统/当前/指定工作目录；活动编辑或待回写内容不进入普通过期清理。 | P3 |
| 真实图标、整行、网格、单击、替代选择、`..` | [SettingsPage2.rc](../third_party/7zip/CPP/7zip/UI/FileManager/SettingsPage2.rc):10–15 | Qt 视图设置保留相同控制范围，默认值适配 macOS。 | P3 |
| 归档/标准工具栏、大按钮、文字开关 | [resource.rc](../third_party/7zip/CPP/7zip/UI/FileManager/resource.rc):116–122 | 可配置 toolbar；主要动作保留添加、提取、测试、复制、移动、删除、信息。 | P1、P3 |

## 工具与命令行高级能力

| 功能 | 源码依据 | M7Arc 落点与目标行为 | 阶段 |
| --- | --- | --- | --- |
| CRC-32/64、XXH64、MD5、SHA-1/256/384/512、SHA3-256、BLAKE2sp、全部 | [resource.rc](../third_party/7zip/CPP/7zip/UI/FileManager/resource.rc):56–68 | `HashJob` 复用 `UI/Common/HashCalc`，支持文件、目录和结果复制。 | P5 |
| 生成 SHA256 / SHA1 摘要文件、校验摘要文件 | [ContextMenu.h](../third_party/7zip/CPP/7zip/UI/Explorer/ContextMenu.h):98–100；[CompressDialog.cpp](../third_party/7zip/CPP/7zip/UI/GUI/CompressDialog.cpp) 的 `g_HashMethods` | 保留原版摘要输出入口与十种算法计算；结果包含缺失、不支持算法、失败和通过条目。 | P5 |
| 压缩/解压基准 | [BenchmarkDialog.rc](../third_party/7zip/CPP/7zip/UI/GUI/BenchmarkDialog.rc):70–151 | 线程、字典、passes、速度、CPU 使用、评分、系统信息和日志完整呈现。 | P5 |
| 分割普通文件、合并分卷 | [PanelSplitFile.cpp](../third_party/7zip/CPP/7zip/UI/FileManager/PanelSplitFile.cpp):235、419 | 独立工具，保留尺寸设置、进度和首卷识别。 | P5 |
| CLI 参数解析与帮助 | [ArchiveCommandLine.cpp](../third_party/7zip/CPP/7zip/UI/Common/ArchiveCommandLine.cpp):278、1048、1421 | 随产品提供完整引擎 CLI；GUI 作业使用结构化调用，CLI 供自动化。 | P0、P5 |
| 外部 Codecs/Formats 模块 | [CreateCoder.h](../third_party/7zip/CPP/7zip/Common/CreateCoder.h):13；[LoadCodecs.cpp](../third_party/7zip/CPP/7zip/UI/Common/LoadCodecs.cpp):539、850 | 保留内建格式并接通既有动态模块加载，改为明确的 Bundle/用户扩展目录；模块按当前 ABI 重建为 Mach-O，第三方加载与签名条件见总设计。 | P5–P6 |
| include/exclude、archive include/exclude、递归 | [ArchiveCommandLine.cpp](../third_party/7zip/CPP/7zip/UI/Common/ArchiveCommandLine.cpp):305–313 | 高级选择配置映射原 censor；保留列表文件和 wildcard 行为。 | P2、P5 |
| 指定/排除格式、方法参数 | [ArchiveCommandLine.cpp](../third_party/7zip/CPP/7zip/UI/Common/ArchiveCommandLine.cpp):298–301 | 保留 `-t/-stx/-m`；GUI 高级参数可达对应能力。 | P2、P5 |
| 自定义更新 action、批量重命名 | [ArchiveCommandLine.cpp](../third_party/7zip/CPP/7zip/UI/Common/ArchiveCommandLine.cpp):874、953；[Update.h](../third_party/7zip/CPP/7zip/UI/Common/Update.h):67 | 保留 CLI 的细粒度规则；GUI 提供四种常用更新模式和逐项重命名。 | P2、P5 |
| stdin/stdout、输出流路由、日志等级 | [ArchiveCommandLine.cpp](../third_party/7zip/CPP/7zip/UI/Common/ArchiveCommandLine.cpp):284–291、323–324 | CLI 保留管道；GUI 使用作业日志，不解析终端文本作为业务数据。 | P5 |
| 列表文件/控制台字符集、技术列表 | [ArchiveCommandLine.cpp](../third_party/7zip/CPP/7zip/UI/Common/ArchiveCommandLine.cpp):327–332 | 保留 CLI 编码选项和技术输出；Qt 使用 Unicode 显示。 | P5 |
| 大小写、禁用 wildcard、绝对路径、输出目录模式 | [ArchiveCommandLine.cpp](../third_party/7zip/CPP/7zip/UI/Common/ArchiveCommandLine.cpp):337–344 | 采用 macOS 文件系统实际语义；高级参数沿用引擎定义。 | P2、P5 |
| owner ID/name、链接、归档 NT 属性 | [ArchiveCommandLine.cpp](../third_party/7zip/CPP/7zip/UI/Common/ArchiveCommandLine.cpp):346–359；[Update.h](../third_party/7zip/CPP/7zip/UI/Common/Update.h):99–105 | POSIX 属性与 Windows 属性分开；明确格式可存和本机可恢复的范围。 | P4–P5 |
| 解压内存限额、线程 affinity、大页参数 | [ArchiveCommandLine.cpp](../third_party/7zip/CPP/7zip/UI/Common/ArchiveCommandLine.cpp):315、321、326 | 内存与线程保留；Windows affinity/large pages 只在平台实际支持时暴露。 | P0、P5 |
| Windows SFX 输出 | [UpdateGUI.cpp](../third_party/7zip/CPP/7zip/UI/GUI/UpdateGUI.cpp):31–32；[CompressDialog.rc](../third_party/7zip/CPP/7zip/UI/GUI/CompressDialog.rc):121 | 在 Mac 上生成面向 Windows 的 `.exe`，使用明确版本和方法能力的 Windows stub。 | P5 |
| Unix console SFX 原型 | [SFXCon/makefile.gcc](../third_party/7zip/CPP/7zip/Bundles/SFXCon/makefile.gcc):1、57、64 | 从现有 SFXCon 的非 Windows 构建入口发展终端自解压原型，单独验证可执行文件与载荷定位。 | P5 |
| 安装器 SFX 配置与解压后运行 | [SfxSetup.cpp](../third_party/7zip/CPP/7zip/Bundles/SFXSetup/SfxSetup.cpp):1 | 保留 Windows 发布者输出；Mac 发布者模式显式配置目标平台和安装动作，按整包签名，普通提取不自动运行成员。 | P5–P6 |
| 双击 macOS 自解压器包 | [SfxCon.cpp](../third_party/7zip/CPP/7zip/Bundles/SFXCon/SfxCon.cpp):1；[UpdateGUI.cpp](../third_party/7zip/CPP/7zip/UI/GUI/UpdateGUI.cpp):31 | 默认以签名提取器 `.app` 加同目录 `Payload.7z` 组合 DMG；发布者可内嵌资源载荷，再对完整 app 签名与公证。 | P5–P6 |
| 帮助、关于、语言设置 | [resource.rc](../third_party/7zip/CPP/7zip/UI/FileManager/resource.rc):154–159；[LangPage.cpp](../third_party/7zip/CPP/7zip/UI/FileManager/LangPage.cpp):1 | Qt 本地化、可搜索帮助、上游版本与格式能力说明；帮助替代 CHM。 | P5–P6 |

## macOS 系统集成与属性

| 功能 | 源码依据 | M7Arc 落点与目标行为 | 阶段 |
| --- | --- | --- | --- |
| Shell 快捷提取、提取到、测试、添加 7z/ZIP | [ContextMenuFlags.h](../third_party/7zip/CPP/7zip/UI/Explorer/ContextMenuFlags.h):8–21 | Services/Quick Actions 进入同一作业入口；批量选择路径完整传递。 | P4 |
| Finder 操作菜单 | [ContextMenu.h](../third_party/7zip/CPP/7zip/UI/Explorer/ContextMenu.h):28–64 | 使用 Services 和可安装 Quick Actions 承接操作；菜单项依选择类型及用户启用状态显示。 | P4 |
| 压缩后邮件/分享 | [ContextMenu.cpp](../third_party/7zip/CPP/7zip/UI/Explorer/ContextMenu.cpp):1298–1333 | 生成归档后调用 macOS 分享或邮件草稿；保留用户选择输出与接收应用。 | P4 |
| 文件关联、Open With、Dock 打开文件 | [SystemPage.cpp](../third_party/7zip/CPP/7zip/UI/FileManager/SystemPage.cpp):1；[RegistryAssociations.cpp](../third_party/7zip/CPP/7zip/UI/FileManager/RegistryAssociations.cpp):1 | 按全部 handler 扩展别名合并关联项，保留共享后缀候选；Bundle 文档类型声明与系统打开事件使用同一能力目录，默认应用变更遵循 macOS 用户选择。 | P4、P6 |
| 外部打开、Finder reveal、Quick Look | [PanelItemOpen.cpp](../third_party/7zip/CPP/7zip/UI/FileManager/PanelItemOpen.cpp):820、835 | `MacPlatform` 使用原生打开/定位；包内预览按需提取并管理临时文件。 | P4 |
| 拖入文件、拖出包内文件、跨面板拖放 | [PanelDrag.cpp](../third_party/7zip/CPP/7zip/UI/FileManager/PanelDrag.cpp):630、1750、2882 | Qt drag/drop 配合 macOS file promises，按目标需要生成提取文件。 | P3–P4 |
| 剪贴板文件操作 | [App.h](../third_party/7zip/CPP/7zip/UI/FileManager/App.h):150–153；[PanelMenu.cpp](../third_party/7zip/CPP/7zip/UI/FileManager/PanelMenu.cpp):57 | 替换 Shell verbs 和 Windows 数据对象；按 macOS 文件复制/移动约定实现。 | P4 |
| 主目录、卷、网络位置 | [RootFolder.cpp](../third_party/7zip/CPP/7zip/UI/FileManager/RootFolder.cpp):1；[FSDrives.cpp](../third_party/7zip/CPP/7zip/UI/FileManager/FSDrives.cpp):1；[NetFolder.cpp](../third_party/7zip/CPP/7zip/UI/FileManager/NetFolder.cpp):1 | 根位置 provider 在文件面板中列出 Home、Volumes 和已挂载网络目录；通过地址栏、根命令或收藏夹进入，连接网络位置交给系统。 | P3–P4 |
| 符号链接、硬链接、junction/WSL 对应 | [LinkDialog.rc](../third_party/7zip/CPP/7zip/UI/FileManager/LinkDialog.rc):23–34 | 本机创建 POSIX symlink/hard link；Windows junction/WSL 作为归档属性读取，不伪装成 APFS 对象。 | P4 |
| NTFS Alternate Streams 与 macOS 扩展属性 | [IFolder.h](../third_party/7zip/CPP/7zip/UI/FileManager/IFolder.h):43；[CompressOptionsDialog.rc](../third_party/7zip/CPP/7zip/UI/GUI/CompressOptionsDialog.rc):30 | 保留包内 streams 浏览；新增 xattr/resource fork 保存与恢复策略，两类数据独立建模。 | P4 |
| Windows ACL 与 POSIX 权限/ACL | [CompressOptionsDialog.rc](../third_party/7zip/CPP/7zip/UI/GUI/CompressOptionsDialog.rc):32；[ExtractDialog.rc](../third_party/7zip/CPP/7zip/UI/GUI/ExtractDialog.rc):54 | 展示原安全属性；macOS 权限、owner、ACL 按格式与本机能力恢复，不直接套用 Windows 描述符。 | P4 |
| Zone.Identifier 与 quarantine | [MenuPage2.rc](../third_party/7zip/CPP/7zip/UI/FileManager/MenuPage2.rc):15；[IFolderArchive.h](../third_party/7zip/CPP/7zip/UI/Agent/IFolderArchive.h):100 | 明确两种来源标记的读取与传播规则；预览、外部打开和提取遵循 macOS quarantine 语义。 | P4 |
| 设置持久化与工作目录 | [ZipRegistry.cpp](../third_party/7zip/CPP/7zip/UI/Common/ZipRegistry.cpp):10；[FoldersPage2.rc](../third_party/7zip/CPP/7zip/UI/FileManager/FoldersPage2.rc):4 | `QSettings`、标准应用支持目录、标准临时目录；替换注册表读写。 | P3–P4 |
| 系统菜单、图标、窗口与帮助资源 | [FM.mak](../third_party/7zip/CPP/7zip/UI/FileManager/FM.mak):7；[ArchiveFolderOpen.cpp](../third_party/7zip/CPP/7zip/UI/Agent/ArchiveFolderOpen.cpp):13 | Qt Widgets 与 macOS 原生桥接；图标移到 Qt/Bundle 资源，不继续依赖 HWND/HMODULE。 | P0–P4 |
| FAR 集成功能 | [Far/Plugin.h](../third_party/7zip/CPP/7zip/UI/Far/Plugin.h):20、59–89 | 列举、提取、更新、删除由共享 Agent 与 Qt 前端承接；不构建 FAR DLL。 | P0–P3 |

## 现有行为与迁移修正

| 现有行为 | 可达场景与源码 | M7Arc 修正 | 阶段 |
| --- | --- | --- | --- |
| 嵌套归档回写询问中的 Cancel 会放弃改动 | 修改内层归档后返回上层；[PanelItemOpen.cpp](../third_party/7zip/CPP/7zip/UI/FileManager/PanelItemOpen.cpp):612 只处理 IDYES，:624 随后删临时子包；[PanelFolderChange.cpp](../third_party/7zip/CPP/7zip/UI/FileManager/PanelFolderChange.cpp):999–1011 无条件弹栈。 | 返回保存/放弃/取消三种结果；取消保留当前层、临时文件和改动；父层写入失败保留可恢复子包。 | P3 |
| 外部编辑回写依赖当前面板目录与条目索引 | 编辑期间切换目录时，[PanelItemOpen.cpp](../third_party/7zip/CPP/7zip/UI/FileManager/PanelItemOpen.cpp):1068 返回；:1074–1086 说明并按名称处理归档变化后的索引匹配，同路径重复条目可能被定位为另一项。 | 编辑会话绑定原 ArchiveSession 和具体条目；面板导航不终止回写；原归档重载导致身份不再确定时要求选择目标或另存。 | P3 |
| 从归档向外移动未实现 | [ArchiveFolder.cpp](../third_party/7zip/CPP/7zip/UI/Agent/ArchiveFolder.cpp):32–37 对 moveMode 返回 E_NOTIMPL；[App.cpp](../third_party/7zip/CPP/7zip/UI/FileManager/App.cpp):793 将移动标志传入该路径。 | 增加目标提取/提交成功后删除源条目的流程；目标失败保留源，删除失败报告为“已复制、源未删除”。 | P3 |

## 已知限制

Windows 可执行 SFX、Unix console SFX 和双击 macOS 提取器是三种不同交付。Mac 默认提取器读取同目录 payload，应用本体签名保持有效；内嵌资源载荷的发布者方案需要对完整 app 签名。P5 的 console 原型不等于 P6 的已签名图形提取器。

macOS xattr、resource fork、Finder 信息和 ACL 是产品必须补齐的能力。每种输出格式分别定义可存储内容与互操作方式；7-Zip 能打开某格式不代表它已提供这些属性的完整往返。归档中保留 Windows 属性与在 macOS 上恢复 Windows 文件系统行为是不同能力。

原资源文件中存在被注释的入口，例如剪切/复制/粘贴菜单、显示已删除文件、解压 Alternate Streams 复选框。表中的剪贴板和 macOS 属性功能是 M7Arc 的明确目标；被注释文字本身不作为现有可见功能依据。所有动态格式与属性以活跃 handler 和接口结果为准。
