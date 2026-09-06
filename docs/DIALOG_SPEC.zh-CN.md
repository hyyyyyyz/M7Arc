# M7Arc 对话框与任务交互规格

M7Arc 保留 7-Zip File Manager 的完整参数表与工具对话框。主窗口的添加、提取、复制、移动、删除、信息，以及文件菜单、工具菜单和 CRC/SHA 菜单，都进入同一组能力驱动的对话框。Qt Widgets 使用 `QDialog`、`QFormLayout`、`QGroupBox`、`QTabWidget` 和 `QTableView`；常用压缩参数默认可见，只有原版单独的 Options 与扩展方法进入折叠区。

对话框的数据来源分三层：官方 handler / codec / hasher 注册表决定引擎能力，当前文件夹与所选项目决定命令是否可用，任务结果决定进度与后续提示。文件扩展名只用于建议格式和显示关联，不用于推定编码能力。

## 添加到压缩包

源码依据：[CompressDialog.rc](../third_party/7zip/CPP/7zip/UI/GUI/CompressDialog.rc)、[CompressDialog.cpp](../third_party/7zip/CPP/7zip/UI/GUI/CompressDialog.cpp) 的 `FormatChanged`、`SetMethod2`、`SetDictionary2`、`SetOrder2`、`SetNumThreads2`、`SetMemoryUsage`，以及 [UpdateGUI.cpp](../third_party/7zip/CPP/7zip/UI/GUI/UpdateGUI.cpp) 的可更新格式筛选。

窗口顶部保留归档名称、输出目录与路径历史。下方为与 Windows 原版相同的两列：左列依次放格式、等级、方法、字典、词大小、固实块、线程、内存、分卷与参数；右列放更新模式、路径模式、操作选项与加密。底部是更多选项、取消、执行。切换格式不关闭窗口。

| 字段 | 内容与联动 | Qt 模块 |
| --- | --- | --- |
| 归档名称 | 可编辑路径、历史、浏览；格式改变时跟随修改建议扩展名，保留用户自定义名称 | `ArchiveNameField` |
| 归档格式 | 直接创建：7z、ZIP、GZip、BZip2、XZ、TAR、WIM；组合预设：tar.gz、tar.bz2、tar.xz | `ArchiveFormatModel` |
| 压缩等级 | 7z：0–9；ZIP：0/1/3/5/7/9；GZip：1/5/7/9；BZip2：1/3/5/7/9；XZ：1–9；TAR/WIM：0 | `CompressionOptionsModel` |
| 压缩方法 | 来自选定格式的有效编码方法；“自动”使用官方默认；等级 0 为 Store，不再显示普通算法参数 | `MethodCapabilities` |
| 字典 / 内存大小 | LZMA/LZMA2 字典；PPMd 内存；BZip2 块大小；Deflate/Deflate64 固定字典只读显示 | `MethodParametersWidget` |
| 词大小 / 模型阶数 | LZMA/LZMA2 的匹配词长；PPMd 的模型阶数；Deflate/Deflate64 的 fast bytes；PPMd ZIP 使用自身上限 | `MethodParametersWidget` |
| 固实块大小 | 非固实、有限块、全部固实、自动；7z 与 XZ 按各自方法处理，其他格式不可选 | `SolidBlockOptions` |
| CPU 线程 | 自动、有效线程数；格式与算法可用线程数、硬件线程数及内存上限共同限制 | `ThreadOptions` |
| 内存 | 显示压缩估计、解压估计、允许内存比例；使用官方计算，字典/线程/等级变化后刷新 | `MemoryEstimate` |
| 分卷 | 原始字节及 K/M/G/T 后缀，多个大小序列；后续分卷沿用最后一个值；SFX 时禁用 | `VolumeSizeParser` |
| 参数 | 官方方法属性字符串；与结构化参数组合，统一显示最终有效配置 | `RawPropertiesEditor` |
| 更新模式 | 添加并替换、更新并添加、只更新已有、同步；映射官方 update action set | `UpdateMode` |
| 路径模式 | 相对路径、完整路径、绝对路径；与所选输入根目录共同决定存储名称 | `CensorPathMode` |
| 操作选项 | 创建 SFX、压缩共享文件、压缩成功后删除源文件 | `UpdateOptions` |
| 加密 | 密码、确认密码、显示密码、加密方法、加密文件名；按格式启用 | `EncryptionOptions` |

原 Windows 的方法下拉菜单不能直接等同于所有已编译方法。7z 默认下拉实际显示 LZMA2、LZMA、PPMd、BZip2；`SetMethod2` 跳过 Copy、Deflate、Deflate64。M7Arc 保留这些默认项，在更多选项提供引擎高级方法，Store 仍由等级 0 表达。ZIP 默认方法为 Deflate、Deflate64、BZip2、LZMA、PPMd。TAR 的方法字段表示 GNU / POSIX 头格式；WIM 在原版添加窗口只提供 Store。Zstandard 在此版本为解码能力，不能显示为创建格式。

GZip、BZip2、XZ 直接创建只接受一个文件。选择多个文件或文件夹时，在原窗口中解释单流限制并提供对应 TAR 组合预设；组合执行计划为“创建临时 TAR → 压缩 TAR → 完成输出”。TAR 组合不是新增 handler，格式中心保留二者区别。压缩密码仅支持 7z AES-256，以及 ZIP 的 ZipCrypto / AES-256；文件名加密仅对 7z 启用。

选择 SFX 后输出扩展名为 `.exe`，方法限制为官方 SFX 支持的集合；取消 SFX 后恢复归档扩展名。格式切换到不支持 SFX 的格式时清除该状态。共享文件选项映射读取策略。删除源文件必须在归档操作成功后执行，并通过正常任务结果返回失败项。

每种格式保留上次使用的有效参数；输入文件变化后重新判定单流资格和更新能力。密码保存在任务内存中，不写入普通设置。对话框提交 `UpdatePlan`，后台工作线程执行官方更新流程，界面不拼装 Shell 命令。

## 更多压缩选项

源码依据：[CompressOptionsDialog.rc](../third_party/7zip/CPP/7zip/UI/GUI/CompressOptionsDialog.rc)，以及 `CCompressDialog::FormatChanged` 内的 `Flags_SymLinks`、`Flags_HardLinks`、`Flags_AltStreams`、`Flags_NtSecurity`。

| 组 | 完整字段 | 语义 |
| --- | --- | --- |
| 链接与元数据 | 存储符号链接、硬链接、NTFS 备用数据流、NTFS 文件安全信息 | 按输入文件系统与格式能力启用；POSIX 链接与 NTFS 数据分别处理 |
| 时间精度 | 格式默认，或该格式实际支持的秒 / 亚秒精度 | 不能把某个格式不支持的精度展示为有效输出 |
| 时间字段 | 修改时间、创建时间、最后访问时间 | 每项区分“格式默认 / 存储 / 不存储”，对应源码中的 Defined 与 Value |
| 归档时间 | 使用格式默认，或取最新文件时间，或关闭 | 保留显式覆盖与默认之间的区别 |
| 输入访问时间 | 不改变源文件的最后访问时间 | 映射 `PreserveATime` |
| 高级编码 | 更多有效方法、预处理滤镜与原始方法属性 | 来自已编译编码器/滤镜注册表；方法组合必须由引擎接受 |

macOS 对符号链接、硬链接、POSIX mode、时间和扩展属性提供独立文件系统实现。NTFS 安全描述符与 alternate streams 可作为归档内元数据浏览、复制或提取到明确的表示形式，不直接套用为 macOS ACL 或任意扩展属性。无法实际恢复的字段在对应任务中给出具体结果。

## 提取文件

源码依据：[ExtractDialog.rc](../third_party/7zip/CPP/7zip/UI/GUI/ExtractDialog.rc)、[ExtractDialog.cpp](../third_party/7zip/CPP/7zip/UI/GUI/ExtractDialog.cpp)、[ExtractMode.h](../third_party/7zip/CPP/7zip/UI/Common/ExtractMode.h)。

左列为目标路径、同名子文件夹开关及名称、路径模式、消除重复根文件夹、覆盖模式。右列为密码、显示密码与元数据恢复。目标文件夹使用可编辑历史与浏览按钮；选中的归档内目录作为当前路径模式的根。

| 模式 | 输入示例 | 输出 |
| --- | --- | --- |
| 完整路径 | `a/b/readme.txt` | `目标/a/b/readme.txt` |
| 当前路径 | 当前已进入 `a/`，选中 `b/readme.txt` | `目标/b/readme.txt` |
| 不含路径 | `a/b/readme.txt` | `目标/readme.txt` |
| 绝对路径 | 归档中记录绝对位置 | 按记录的绝对位置写入 |

Windows 的提取对话框直接列出完整、不含、绝对三种路径；`kCurPaths` 也存在于官方公共 API。M7Arc 将其作为归档内选中目录的明确选项；绝对路径放入高级折叠区。启用绝对模式后普通模式控件禁用，避免互相矛盾的输入。

覆盖模式保留五种：询问、直接覆盖、跳过已有文件、自动重命名传入文件、自动重命名已有文件。消除重复根文件夹与“创建同名子文件夹”独立控制。密码可由任务按需请求，输入错误后保留当前目标、选择范围和其他参数以便重新提交。

恢复选项包括时间、POSIX 权限，以及由输入与目标能力决定的链接和元数据。NTFS 文件安全选项只有在实际可表达的目标上启用。

## 覆盖、密码与内存请求

源码依据：[OverwriteDialog.rc](../third_party/7zip/CPP/7zip/UI/FileManager/OverwriteDialog.rc)、[PasswordDialog.rc](../third_party/7zip/CPP/7zip/UI/FileManager/PasswordDialog.rc)、[MemDialog.rc](../third_party/7zip/CPP/7zip/UI/FileManager/MemDialog.rc)。

覆盖请求并列显示已有文件与传入文件的路径、大小、修改时间和类型。动作完整保留替换、全部替换、自动重命名、跳过、全部跳过、取消；一次任务中的“全部”只影响当前任务。所有请求由任务控制器排队投递到主线程，工作线程等待明确动作。

密码请求显示当前归档路径、密码输入和显示开关。嵌套归档可以分别请求不同密码；任务内按归档层记录成功使用的密码，不把一个归档的输入视为所有归档的有效密码。

内存请求显示所需内存和当前上限，允许本次解压、跳过归档、取消任务；可选择修改后续操作的上限及本次重复使用选定动作。超限请求应使用官方回调数据。减少线程仅在当前方法允许且确实降低内存需求时提供。

## 任务进度与错误结果

源码依据：[ProgressDialog2.rc](../third_party/7zip/CPP/7zip/UI/FileManager/ProgressDialog2.rc)、[ProgressDialog2.cpp](../third_party/7zip/CPP/7zip/UI/FileManager/ProgressDialog2.cpp)、[MessagesDialog.cpp](../third_party/7zip/CPP/7zip/UI/FileManager/MessagesDialog.cpp)。

任务状态为等待、扫描、执行、等待用户、暂停、取消中、已取消、完成、部分失败、失败。暂停只阻止后续工作；取消先通知引擎，再等待工作线程结束和输出收尾。切换到后台隐藏进度窗口，主窗口状态区继续显示任务。

显示总进度、当前文件、已处理/总文件数、已处理/总字节数、速度、已用时间、预计剩余时间；压缩任务额外显示输入、输出和压缩率。测试和摘要任务用同一进度事件，不虚构压缩率。错误结果保存具体文件与引擎错误文本，并允许复制；完成结果区分所有成功、部分跳过、数据错误和取消。

Qt 的 `JobController` 持有取消/暂停状态，`EngineWorker` 在线程中使用官方 callback，Qt queued signal 更新 `ProgressModel`。所有 GUI 提示请求都在 GUI 线程显示，后台线程不直接操作 QWidget。

## 性能测试

源码依据：[BenchmarkDialog.rc](../third_party/7zip/CPP/7zip/UI/GUI/BenchmarkDialog.rc)、[BenchmarkDialog.cpp](../third_party/7zip/CPP/7zip/UI/GUI/BenchmarkDialog.cpp)。

保留字典大小、CPU 线程、内存需求、轮数、重新开始、停止。结果表分别显示压缩/解压的当前值与累计值，列为数据大小、速度、CPU 使用率、评分/使用率、评分。下方显示总体评分、已用时间、完成轮数、CPU 型号/特性、版本、系统信息和日志。

参数改变时停止当前轮次统计并明确要求重启；重新开始清空本轮累计结果。完整 benchmark 模式的文本日志保留独立输出视图。正式功能调用官方 benchmark 实现，不用普通压缩耗时替代 MIPS 评分。

原型的开始、停止、重启、轮数及参数变更均可交互，结果始终标为“示例”。这些数值不作为本机或 Apple Silicon 的性能证据。

## CRC / SHA

源码依据：[HashGUI.cpp](../third_party/7zip/CPP/7zip/UI/GUI/HashGUI.cpp) 的 `AddHashBundleRes`、[HashCalc.cpp](../third_party/7zip/CPP/7zip/UI/Common/HashCalc.cpp) 与当前构建的 [7zz 格式与算法清单](evidence/7zz-info.txt)。

算法选择展示当前完整注册表：CRC32、CRC64、MD5、SHA1、SHA256、SHA384、SHA512、SHA3-256、XXH64、BLAKE2sp。支持菜单直接运行某算法、选择多种、全部算法；不能凭 Hash handler 接受的文件后缀增加未注册算法。

单个文件显示名称、大小和各算法摘要。多个文件显示文件夹数、文件数、总字节数、文件数据总和，以及数据与文件名总和；存在 alternate streams 时显示流数量、大小与对应摘要。错误数单列。摘要值、文件名与错误文本可复制。

“保存摘要文件”保留原 GUI 的 SHA256 / SHA1 输出入口；打开摘要文件的 handler 能力与计算算法列表分开。文件面板中的校验文件测试进入验证任务。此处的摘要是用户主动要求的 7-Zip 文件工具功能。

## 分割与合并

源码依据：[SplitDialog.rc](../third_party/7zip/CPP/7zip/UI/FileManager/SplitDialog.rc)、[SplitUtils.cpp](../third_party/7zip/CPP/7zip/UI/FileManager/SplitUtils.cpp)、[PanelSplitFile.cpp](../third_party/7zip/CPP/7zip/UI/FileManager/PanelSplitFile.cpp)。

分割只处理文件系统中的一个普通文件。输入为目标目录和分卷大小序列；预设保留 10M、100M、1000M、650M CD、700M CD、4092M FAT、4480M DVD、8128M DVD DL、23040M BD。大小按官方解析语义解释，分卷名按 `.001` 起递增。显示预计分卷数；分卷大小不小于文件大小时提示不需要分割。

合并从选定的首个分卷按编号枚举连续文件，显示源目录、分卷数量、总大小、前后分卷名称及目标目录。输出名称去掉分卷后缀。合并按钮仅在找到有效序列后可用；目标存在、只有一个分卷、总数据为空都返回具体原因。分卷归档通常可直接打开首卷提取；独立合并功能仍保留，适用于普通分割文件。

## 复制、移动、重命名与创建

源码依据：[CopyDialog.rc](../third_party/7zip/CPP/7zip/UI/FileManager/CopyDialog.rc)、[PanelCopy.cpp](../third_party/7zip/CPP/7zip/UI/FileManager/PanelCopy.cpp)、[PanelMenu.cpp](../third_party/7zip/CPP/7zip/UI/FileManager/PanelMenu.cpp)。

复制/移动显示操作范围、目标位置及路径历史；双面板模式默认另一面板目录。源/目标可以是文件系统或支持更新的归档，实际路径决定本地复制、提取、添加或归档间传递计划。只读归档的修改命令在主窗口禁用，并给出能力说明。

重命名、创建文件夹、创建文件复用简短输入对话框。目标文件夹由调用面板提供，不在对话框中重新猜测。删除进入带项目数量与目标范围的确认，已授权的拖放移动和菜单移动仍保持各自语义。

创建链接保留源位置、目标位置及链接类型；macOS 提供硬链接、文件符号链接、目录符号链接。Windows 的 Directory Junction 和 WSL reparse point 仍可作为归档元数据呈现，不列为 macOS 创建类型。字段依据 [LinkDialog.rc](../third_party/7zip/CPP/7zip/UI/FileManager/LinkDialog.rc)。

注释编辑读取所选项目的 `kpidComment`，通过 `IFolderOperations::SetProperty` 写回；Hash 文件夹不提供该操作。字段依据 [PanelOperations.cpp](../third_party/7zip/CPP/7zip/UI/FileManager/PanelOperations.cpp) 的 `ChangeComment`。

## 属性与归档内编辑

源码依据：[PanelMenu.cpp](../third_party/7zip/CPP/7zip/UI/FileManager/PanelMenu.cpp) 的 `CPanel::Properties`，以及 [PanelItemOpen.cpp](../third_party/7zip/CPP/7zip/UI/FileManager/PanelItemOpen.cpp) 的 `OpenParentArchiveFolder`、`OnOpenItemChanged`。

属性界面分为所选文件、当前文件夹、当前归档及各外层归档。文件属性通过 `GetNumberOfProperties` / `GetPropertyInfo` 枚举，原始属性通过 `GetNumRawProps` 枚举；文件夹通过 `IFolderProperties`，嵌套归档通过 `IFolderArcProps::GetArcNumLevels` 逐层展示。这样可完整显示方法、加密、CRC、固实块、POSIX 属性、符号/硬链接目标、头部/物理大小、偏移、尾部数据、警告与错误，不把不同格式硬套成固定十行。

从归档打开外部编辑器后，`EditSession` 记录临时文件、成员索引、父归档链和实际打开的程序。外部编辑完成或返回父归档时比较文件的实际修改状态；有修改则提示保存回归档、放弃修改、取消。保存逐层回写嵌套归档；失败保留编辑会话和可恢复的临时文件。取消保留当前会话，放弃关闭会话并保留原归档内容。

## 选项窗口

源码依据：[OptionsDialog.cpp](../third_party/7zip/CPP/7zip/UI/FileManager/OptionsDialog.cpp) 注册的六个页面，以及对应的 `SystemPage`、`MenuPage2.rc`、`FoldersPage2.rc`、`EditPage2.rc`、`SettingsPage2.rc`、`LangPage`。

| 页面 | 完整功能 | macOS 映射 |
| --- | --- | --- |
| 系统 | 全部可关联格式、扩展名、当前关联、选择/取消关联 | Launch Services、文档类型、系统默认应用；不复制 Windows 用户/所有用户注册表列 |
| 菜单 | 上下文菜单集成、层级、图标、消除重复根目录、具体命令开关 | Finder 服务、Quick Actions 或可用的 Finder 扩展；下载来源按 macOS 原生规则处理 |
| 文件夹 | 系统临时、当前、指定目录，仅可移动磁盘使用 | `QStandardPaths` 与平台文件系统判断 |
| 查看 / 编辑 / Diff | 分别选择三个外部程序及参数 | `QDesktopServices` / `QProcess` / 原生应用选择 |
| 设置 | “..”条目、真实图标、整行选择、网格、单击打开、替代选择、系统菜单、大内存页、解压内存上限 | Qt 视图行为；平台不支持的大页设置明确禁用；内存上限保留 |
| 语言 | 语言选择、语言信息、立即更新窗口文本 | 随附语言资源 → Qt 翻译目录，稳定 ID 对应官方字符串 |

应用设置后刷新两侧文件面板、工具栏和菜单文本。格式关联由系统层确认结果后回显。原型选项仅改变本次页面会话，不修改系统默认应用。

## SFX 平台边界

源码依据：[SFX 相关 bundles](../third_party/7zip/CPP/7zip/Bundles/) 以及压缩窗口的 `kExeExt`、`g_7zSfxMethods`、`CheckVolumeEnable`。

Windows `.exe` SFX 的创建与读取保留为独立功能。macOS `.app` 自解压器需要原生启动器、提取 UI、代码签名与发布流程，作为平台实现任务，不以改扩展名的方式替代。原型可选择 Windows SFX 模块；macOS SFX 显示尚未实现并禁用提交。

## 原型覆盖与实现边界

[dialogs.js](../design/prototype/dialogs.js) 提供添加、提取、六页选项、性能测试、CRC/SHA、分割、合并、复制、移动、重命名、新建文件/文件夹、属性、编辑回写、覆盖、密码、删除、链接、注释和 SFX 的交互。模块读取 [formats.js](../design/prototype/formats.js) 的格式与方法数据；所有执行按钮明确使用模拟操作，示例结果不代表本地文件操作。

正式 Qt 实现中，文件系统浏览、实际写入、原生应用关联、真实内存估计、压缩/解压进度、性能测量和摘要结果都由对应引擎与平台模块提供。原型的通用属性样例、部分时间精度候选和线程候选用于展示布局；最终候选值由实际引擎方法和平台能力决定。
