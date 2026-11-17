# M7Arc File Manager 功能与窗口规格

M7Arc 的主界面采用 7-Zip File Manager 的文件管理器结构：六组菜单、七个工具栏动作、可编辑地址栏、属性列文件列表和状态栏。默认单面板，F9 切换双面板；磁盘目录、归档目录、嵌套归档、磁盘映像和校验清单都由同一面板呈现。Qt Widgets 承担窗口与交互，官方 provider 和归档接口提供条目、属性和操作能力。

本文的源码基准是仓库内 `third_party/7zip/`。`FM/` 代表 `CPP/7zip/UI/FileManager/`，`GUI/` 代表 `CPP/7zip/UI/GUI/`，`Explorer/` 代表 `CPP/7zip/UI/Explorer/`。源码行号指此副本；“Qt 对应”是 M7Arc 的实现设计。

## 1. 主窗口

```text
M7Arc — /Users/jackson/Downloads/
文件   编辑   查看   收藏夹   工具   帮助
添加    提取    测试    复制    移动    删除    信息
[↑] [ /Users/jackson/Downloads/                           ▾ ]
名称              大小        压缩后大小     修改时间       …
..                                                       
Projects/                                                
source.7z         28 MB                                  
installer.dmg    124 MB                                  
…                                                       
1 / 24 个对象已选择 | 28 MB | 当前文件大小 | 当前文件修改时间
```

Qt 正式应用把六组菜单交给 macOS 顶部菜单栏，关于、偏好设置、退出使用 `QAction::MenuRole` 进入应用菜单。原型在窗口顶部展示完整六组菜单，便于直接核对功能。

| 区域 | 原版行为 | Qt 对应 | 源码依据 |
| --- | --- | --- | --- |
| 窗口与会话 | 保存位置、最大化状态、面板数量、当前面板与分隔条位置；首次打开单面板 | `MainWindow : QMainWindow`，`QSettings` | [FM.cpp:127](../third_party/7zip/CPP/7zip/UI/FileManager/FM.cpp#L127)、[FM.cpp:293](../third_party/7zip/CPP/7zip/UI/FileManager/FM.cpp#L293)、[ViewSettings.h:69](../third_party/7zip/CPP/7zip/UI/FileManager/ViewSettings.h#L69) |
| 顶部菜单 | 文件、编辑、查看、收藏夹、工具、帮助；按当前面板能力更新状态 | `QMenuBar`、共享 `QAction` | [resource.rc:33](../third_party/7zip/CPP/7zip/UI/FileManager/resource.rc#L33)、[MyLoadMenu.cpp:398](../third_party/7zip/CPP/7zip/UI/FileManager/MyLoadMenu.cpp#L398) |
| 工具栏 | 添加 → 提取 → 测试 → 复制 → 移动 → 删除 → 信息；压缩组与标准组分别可显示/隐藏；大按钮与按钮文字可切换 | `QToolBar`；所有入口复用相同动作 | [App.cpp:185](../third_party/7zip/CPP/7zip/UI/FileManager/App.cpp#L185)、[App.cpp:255](../third_party/7zip/CPP/7zip/UI/FileManager/App.cpp#L255) |
| 面板容器 | 1 或 2 面板；切换保留双方目录、选择与视图；Tab 切换焦点 | 水平 `QSplitter`；两个 `FilePanel` | [App.cpp:358](../third_party/7zip/CPP/7zip/UI/FileManager/App.cpp#L358)、[PanelKey.cpp:39](../third_party/7zip/CPP/7zip/UI/FileManager/PanelKey.cpp#L39) |
| 地址栏 | 每个面板独立上一级按钮、可编辑路径下拉、路径历史；输入路径可进入归档内部 | `QToolButton` + 可编辑 `QComboBox`；路径解析归 `FolderSession` | [Panel.cpp:444](../third_party/7zip/CPP/7zip/UI/FileManager/Panel.cpp#L444)、[Panel.cpp:502](../third_party/7zip/CPP/7zip/UI/FileManager/Panel.cpp#L502)、[PanelFolderChange.cpp](../third_party/7zip/CPP/7zip/UI/FileManager/PanelFolderChange.cpp) |
| 文件区 | 详细信息为默认视图；支持大图标、小图标、列表；可显示 `..`；就地重命名；鼠标多选与另一种选择模式 | `QTableView` + `QListView` 共用 `FolderItemModel : QAbstractItemModel` 和 `QItemSelectionModel` | [ViewSettings.h:85](../third_party/7zip/CPP/7zip/UI/FileManager/ViewSettings.h#L85)、[Panel.cpp:391](../third_party/7zip/CPP/7zip/UI/FileManager/Panel.cpp#L391)、[PanelItems.cpp:578](../third_party/7zip/CPP/7zip/UI/FileManager/PanelItems.cpp#L578) |
| 属性列 | 按当前 provider 的全部普通属性和原始属性生成；列显隐、宽度和顺序按文件夹类型保存；名称列始终存在 | `QHeaderView` + 列选择 `QMenu`；保留属性 ID 与数值类型；按类型排序 | [PanelItems.cpp:96](../third_party/7zip/CPP/7zip/UI/FileManager/PanelItems.cpp#L96) |
| 状态栏 | 四段：操作对象数量/总数量、操作对象总大小、焦点文件大小、焦点文件修改时间 | 面板底部 `QWidget` + 四个 `QLabel` | [PanelListNotify.cpp:759](../third_party/7zip/CPP/7zip/UI/FileManager/PanelListNotify.cpp#L759) |
| 右键菜单 | 文件系统面板可加入 7-Zip 动态菜单和系统菜单；归档内采用归档操作菜单；按对象与只读能力禁用动作 | `QMenu` 复用命令注册表；macOS 系统动作由 `MacShellIntegration` 提供 | [PanelMenu.cpp:921](../third_party/7zip/CPP/7zip/UI/FileManager/PanelMenu.cpp#L921)、[MyLoadMenu.cpp:588](../third_party/7zip/CPP/7zip/UI/FileManager/MyLoadMenu.cpp#L588) |

文件管理器首页直接显示文件列表。格式目录、基准测试、任务详情和设置分别打开相应窗口，主工作区保持文件操作密度。

### 1.1 文件列表和归档列表

文件系统 provider 的属性包括名称、大小、修改/创建/访问/变更时间、属性、压缩后大小、inode、链接数、注释、子目录数、子文件数与路径前缀（[FSFolder.cpp:95](../third_party/7zip/CPP/7zip/UI/FileManager/FSFolder.cpp#L95)）。原版初始隐藏部分文件系统列，用户可以选择显示。

归档 provider 可以提供更多列，具体集合由归档返回：大小、压缩后大小、CRC、压缩方法、加密、块、主机操作系统、权限、时间、偏移和链接等。窗口不得把字段限制为名称、大小、类型、日期四列。数字保留原始数值用于排序；日期格式变化只改变显示；未知属性也以其名称与值显示。

进入 `release.tar.xz` 时，地址栏与返回栈保留外部文件、压缩流和 TAR 内部目录层次。进入磁盘映像时，同一模型允许继续进入分区和内层文件系统。`Open Inside *` 与 `Open Inside #` 是独立打开模式，不能都绑定到普通打开。

双面板复制/移动默认使用另一面板当前路径；Shift+F5 / Shift+F6 使用同一面板路径。另一面板可以是可写归档，复制进入归档属于归档更新。对应调度依据 [App.cpp:567](../third_party/7zip/CPP/7zip/UI/FileManager/App.cpp#L567)。

## 2. 菜单和命令

`design/prototype/commands.js` 使用语义 ID；Qt 命令注册表沿用这些 ID，使主菜单、工具栏、右键菜单、快捷键的选择范围、启用条件与操作一致。

### 2.1 文件菜单

静态顺序见 [FM/resource.rc:35](../third_party/7zip/CPP/7zip/UI/FileManager/resource.rc#L35)，行为分发见 [FM/MyLoadMenu.cpp:736](../third_party/7zip/CPP/7zip/UI/FileManager/MyLoadMenu.cpp#L736)。

| 菜单 → 原型 ID | 行为与操作范围 | 控件 → Qt 对应 |
| --- | --- | --- |
| 打开 → `open` | 对选择项优先内部打开可识别归档；普通文件交给系统应用；目录进入当前面板 | `QAction` → `FolderSession` / `QDesktopServices` |
| 内部打开 → `open-inside` | 将焦点项作为归档尝试打开，保留自动识别与嵌套语义 | `QAction` → `ArchiveOpenRequest` |
| 内部打开 * → `open-inside-one` | 向原引擎传 `*` 类型规则，单层打开 | 同上，类型规则独立 |
| 内部打开 # → `open-inside-parser` | 向原引擎传 `#` 解析规则 | 同上，解析模式独立 |
| 外部打开 → `open-outside` | 交系统应用打开；归档内文件先提取到工作目录 | `QAction` → `ExternalOpenSession` |
| 查看 → `view-file` | 调用指定查看器；归档成员先提取 | `QAction` + 查看器配置 |
| 编辑 → `edit-file` | 调用指定编辑器；内容修改后询问是否写回归档 | `QAction` + `ExternalEditSession` + 写回确认 |
| 重命名 → `rename` | 当前目录内就地编辑名称；归档由更新能力决定可用性 | `QStyledItemDelegate` → `FolderOperations` |
| 复制到… → `copy` | 文件系统↔文件系统、文件系统→可写归档、归档→目录、归档间复制 | `CopyMoveDialog` → 作业服务 |
| 移动到… → `move` | 使用复制目标规则，成功后更新源；只读源禁用 | 同上；明确移动模式 |
| 删除 → `delete` | 本地普通删除进废纸篓；永久删除使用独立快捷入口；归档内删除修改归档 | `QAction` + 确认窗口 |
| 分割文件… → `split` | 一个本地文件切成连续分卷；设置目标与分卷字节数 | `SplitDialog` |
| 合并文件… → `combine` | 选择分卷首段识别连续部分并合并；输出完整文件 | `CombineDialog`，复用目标输入布局 |
| 属性 → `info` | 单项属性、多项汇总、目录属性、所有归档层次属性；错误与警告一起显示 | `PropertiesDialog` + `QTableView` |
| 注释… → `comment` | 修改焦点条目的 `kpidComment`；保留 provider 的写入语义 | `CommentDialog` + `QLineEdit` |
| CRC → 算法 ID | 计算选中文件/目录内容摘要，展示结果；见 2.7 | `QMenu` + `HashResultDialog` |
| 比较差异 → `diff` | 调用已配置差异工具比较两个输入；未配置工具时原版隐藏入口 | `QAction` → `QProcess` |
| 新建文件夹 → `create-folder` | 输入文件夹名称；本地与支持更新的归档使用各自 provider | `NameDialog` |
| 新建文件 → `create-file` | 输入文件名，创建空文件；受 provider 能力约束 | `NameDialog` |
| 链接… → `link` | 显示源、目标与链接类型；单项选择可用 | `LinkDialog` |
| 备用数据流 → `streams` | 进入当前对象的备用流 provider；不支持该能力时禁用 | `QAction` → `StreamFolderProvider` |
| 退出 → `exit` | 关闭应用，持久化会话；处理仍在执行的操作与编辑会话 | `QAction::QuitRole` |

原版对只读目录禁用重命名、移动、删除、注释、新建文件与新建目录；分割/合并要求恰好一个本地文件；链接要求恰好一个对象；备用流由 provider 支持决定。校验清单文件夹也单独禁用打开、复制到、编辑等不适用动作（[MyLoadMenu.cpp:633](../third_party/7zip/CPP/7zip/UI/FileManager/MyLoadMenu.cpp#L633)）。M7Arc 直接按这些真实能力设置 `QAction::setEnabled()`。

### 2.2 文件系统面板的动态入口

[FM/PanelMenu.cpp:938](../third_party/7zip/CPP/7zip/UI/FileManager/PanelMenu.cpp#L938) 在文件系统面板建立 7-Zip 上下文菜单，工具栏中的压缩、提取、测试在这里也可到达。级联方式和条目开关由设置决定。

| 动态菜单 | 行为 | 原型 ID / Qt 对应 | 源码 |
| --- | --- | --- | --- |
| 打开压缩包；按类型打开 | 自动打开；`*`、`#`、`#:e`、`7z`、`zip`、`cab`、`rar` 类型规则 | `open`、`open-inside-one`、`open-inside-parser`、`open-inside-parser-exact`、`open-as-…`；`QMenu` | [ContextMenu.cpp:523](../third_party/7zip/CPP/7zip/UI/Explorer/ContextMenu.cpp#L523)、[ContextMenu.cpp:750](../third_party/7zip/CPP/7zip/UI/Explorer/ContextMenu.cpp#L750) |
| 提取文件… | 显示完整提取设置 | `extract`；`ExtractDialog` | [ContextMenu.cpp:839](../third_party/7zip/CPP/7zip/UI/Explorer/ContextMenu.cpp#L839) |
| 提取到当前位置 | 无路径对话框，按当前目录提取 | `extract-here`；提取作业 | [ContextMenu.cpp:848](../third_party/7zip/CPP/7zip/UI/Explorer/ContextMenu.cpp#L848) |
| 提取到“归档名称/” | 每个归档使用对应输出文件夹；可消除重复根目录 | `extract-folder`；提取作业 | [ContextMenu.cpp:857](../third_party/7zip/CPP/7zip/UI/Explorer/ContextMenu.cpp#L857) |
| 测试压缩包 | 完整读取并验证归档，不写出成员 | `test`；测试作业 | [ContextMenu.cpp:870](../third_party/7zip/CPP/7zip/UI/Explorer/ContextMenu.cpp#L870) |
| 添加到压缩包… | 完整添加对话框 | `add`；`AddArchiveDialog` | [ContextMenu.cpp:917](../third_party/7zip/CPP/7zip/UI/Explorer/ContextMenu.cpp#L917) |
| 压缩并通过电子邮件发送… | 先压缩，再创建带附件的邮件草稿 | `compress-email`；压缩作业 + macOS 共享服务 | [ContextMenu.cpp:931](../third_party/7zip/CPP/7zip/UI/Explorer/ContextMenu.cpp#L931) |
| 添加到 .7z / 添加到 .zip | 使用命名与格式预设直接添加 | `compress-7z`、`compress-zip` | [ContextMenu.cpp:941](../third_party/7zip/CPP/7zip/UI/Explorer/ContextMenu.cpp#L941)、[ContextMenu.cpp:974](../third_party/7zip/CPP/7zip/UI/Explorer/ContextMenu.cpp#L974) |
| 压缩到 .7z / .zip 并通过电子邮件发送 | 使用预设压缩，再创建邮件草稿 | `compress-7z-email`、`compress-zip-email` | [ContextMenu.cpp:960](../third_party/7zip/CPP/7zip/UI/Explorer/ContextMenu.cpp#L960)、[ContextMenu.cpp:993](../third_party/7zip/CPP/7zip/UI/Explorer/ContextMenu.cpp#L993) |
| CRC SHA | 十种摘要、全部算法、生成 SHA-256 清单、测试校验清单 | 摘要 ID、`hash-save`、`hash-verify` | [ContextMenu.cpp:294](../third_party/7zip/CPP/7zip/UI/Explorer/ContextMenu.cpp#L294)、[ContextMenu.cpp:1030](../third_party/7zip/CPP/7zip/UI/Explorer/ContextMenu.cpp#L1030) |
| 系统 | 原版 Shell 扩展动作；macOS 使用打开方式、在 Finder 显示、共享、信息等系统动作 | `MacShellIntegration`；按对象构建 | [PanelMenu.cpp:688](../third_party/7zip/CPP/7zip/UI/FileManager/PanelMenu.cpp#L688) |

### 2.3 编辑菜单

顺序见 [FM/resource.rc:80](../third_party/7zip/CPP/7zip/UI/FileManager/resource.rc#L80)。所有选中操作影响当前面板；Qt 使用共享 `QItemSelectionModel`。

| 菜单 → ID | 行为 |
| --- | --- |
| 全选 → `select-all` | 选择所有实际条目，跳过 `..` |
| 取消全选 → `deselect-all` | 清空选择 |
| 反向选择 → `invert-selection` | 对每个实际条目取反 |
| 选择… → `select-mask` | 输入通配符，匹配文件名；默认 `*` |
| 取消选择… → `deselect-mask` | 通配符匹配后移除选择 |
| 按类型选择 → `select-type` | 焦点为目录则选择目录；焦点为文件则按扩展名；无扩展名文件单独处理 |
| 按类型取消选择 → `deselect-type` | 按相同类型规则移除选择 |

匹配与按类型语义见 [PanelSelect.cpp:154](../third_party/7zip/CPP/7zip/UI/FileManager/PanelSelect.cpp#L154)。菜单资源中的剪切、复制、粘贴被注释，当前静态编辑菜单不显示它们；Ctrl+X/C/V 与 Ctrl+Insert、Shift+Insert 在 [PanelKey.cpp:172](../third_party/7zip/CPP/7zip/UI/FileManager/PanelKey.cpp#L172)、[PanelKey.cpp:283](../third_party/7zip/CPP/7zip/UI/FileManager/PanelKey.cpp#L283) 有实际处理。M7Arc 保留剪贴板文件操作，macOS 同时支持 Cmd+C/V；目标为归档时调用添加作业。

### 2.4 查看菜单

顺序见 [FM/resource.rc:95](../third_party/7zip/CPP/7zip/UI/FileManager/resource.rc#L95)；动态勾选与时间精度菜单见 [FM/MyLoadMenu.cpp:415](../third_party/7zip/CPP/7zip/UI/FileManager/MyLoadMenu.cpp#L415)。

| 菜单 → ID | 状态与行为 | Qt 对应 |
| --- | --- | --- |
| 大图标 / 小图标 / 列表 / 详细信息 → `view-large/small/list/details` | 四选一，分别保存到面板；默认详细信息 | `QActionGroup` + `QListView` / `QTableView` |
| 名称 / 类型 / 日期 / 大小 / 不排序 → `sort-name/type/date/size/none` | 五选一；同列再次排序切换方向；不排序尊重 provider 次序 | `QActionGroup` + 排序代理 |
| 平面视图 → `flat-view` | 递归列出子目录内容，路径前缀列仍可区分同名成员 | 可勾选 `QAction` + provider 平面模式 |
| 双面板 → `two-panels` | 单/双面板切换，F9；保留面板状态 | 可勾选 `QAction` + `QSplitter` |
| 时间显示 → `time-menu` | 显示日期、到分钟、到秒、NTFS 100 ns 精度、纳秒精度；另可勾选 UTC | `QActionGroup` + UTC 动作 |
| 工具栏 → `toolbars-menu` | 压缩工具栏、标准工具栏、大按钮、按钮文字四个开关 | `QToolBar` 显隐、图标大小、文字样式 |
| 打开根文件夹 → `root` | 打开本机根 provider；macOS 展示文件系统根、可访问卷及已挂载位置 | `FolderSession` |
| 向上一级 → `up` | 普通目录取父级；归档根返回包所在目录；嵌套容器逐层退出 | `FolderSession` 返回栈 |
| 文件夹历史… → `history` | 可选中跳转，可删除历史项 | `HistoryDialog` |
| 刷新 → `refresh` | 重载当前 provider，恢复可保留的焦点与选择 | `FolderItemModel` |
| 自动刷新 → `auto-refresh` | 开关本地目录变化刷新；归档作业完成后定向刷新相关面板 | `QFileSystemWatcher` + 作业事件 |

时间子菜单在原版用当前日期样例作为标题与条目，资源中的“2017 / Time”是运行时占位，不是实际年份选项。精度只限制显示，不降低引擎读取或存储的精度。

### 2.5 收藏夹、工具、帮助

| 菜单 | 控件和完整行为 | Qt 对应 / 源码 |
| --- | --- | --- |
| 收藏夹 → 添加当前文件夹为 → 书签 0–9 | 十个固定槽；Alt+Shift+数字保存当前虚拟路径；已有槽可替换 | `QMenu`、`QSettings`；[MyLoadMenu.cpp:508](../third_party/7zip/CPP/7zip/UI/FileManager/MyLoadMenu.cpp#L508) |
| 收藏夹 → 0–9 | Alt+数字打开；显示保存路径；支持归档内路径 | `favorite-0` … `favorite-9`；同上 |
| 工具 → 选项… | 六个页面：系统、7-Zip 集成、文件夹、编辑器、设置、语言 | `PreferencesDialog : QDialog` + `QTabWidget`；[OptionsDialog.cpp:34](../third_party/7zip/CPP/7zip/UI/FileManager/OptionsDialog.cpp#L34) |
| 工具 → 基准测试 | 独立的压缩/解压性能窗口，字典、CPU 线程、轮数、当前/结果数据、日志 | `BenchmarkDialog`；[BenchmarkDialog.rc:66](../third_party/7zip/CPP/7zip/UI/GUI/BenchmarkDialog.rc#L66) |
| 工具 → 删除临时文件… | 打开专用浏览器，列举工作临时文件，可进入目录、过滤、选择、删除、刷新和查看属性 | `TempFilesDialog`；[MyLoadMenu.cpp:931](../third_party/7zip/CPP/7zip/UI/FileManager/MyLoadMenu.cpp#L931)、[BrowseDialog2.rc:12](../third_party/7zip/CPP/7zip/UI/FileManager/BrowseDialog2.rc#L12) |
| 帮助 → 内容… | 使用说明，包括命令、对话框选项、格式、命令行 | `HelpWindow` + `QTextBrowser`；[MyLoadMenu.cpp:920](../third_party/7zip/CPP/7zip/UI/FileManager/MyLoadMenu.cpp#L920) |
| 帮助 → 关于 | 项目版本、官方引擎版本、版权、许可证、官网与项目链接 | `AboutDialog`；[AboutDialog.rc](../third_party/7zip/CPP/7zip/UI/FileManager/AboutDialog.rc) |

### 2.6 键盘与拖放

保留 F2 重命名、F3 查看、F4 编辑、F5 复制、F6 移动、F7 新建目录、F9 双面板、Enter 打开、Shift+Enter 外部打开、Backspace 上级。macOS 没有小键盘的键盘也可通过菜单完成选择与反选；Cmd+A、Cmd+C、Cmd+V、Cmd+R、Cmd+Q 和 Cmd+, 提供系统习惯入口。原版 Ctrl+Z 表示“注释”，保留为 Control+Z；Cmd+Z 留给输入控件正常撤销。

其他可达键盘行为：Shift+F4 新建文件；Shift+F5/F6 同目录复制/移动；Alt+F1/F2 定位相应面板地址栏；Alt+Up 把另一面板设为当前目录；Alt+Left/Right 在另一面板打开当前子目录；Insert 在另一种选择模式下切换条目；Shift+方向键扩大选择。依据 [PanelKey.cpp:70](../third_party/7zip/CPP/7zip/UI/FileManager/PanelKey.cpp#L70)、[PanelKey.cpp:123](../third_party/7zip/CPP/7zip/UI/FileManager/PanelKey.cpp#L123)、[PanelKey.cpp:194](../third_party/7zip/CPP/7zip/UI/FileManager/PanelKey.cpp#L194)。

拖放覆盖本地项目到窗口压缩、归档成员拖出提取、面板间复制/移动、拖入可写归档更新。Qt 侧采用 `QMimeData` 文件 URL 与 `QDrag`；归档成员需要先物化时由提取作业提供实际文件。源/目标、复制/移动语义保留在作业请求中。对应模块为 [PanelDrag.cpp](../third_party/7zip/CPP/7zip/UI/FileManager/PanelDrag.cpp)、[PanelCopy.cpp](../third_party/7zip/CPP/7zip/UI/FileManager/PanelCopy.cpp)、[App.cpp](../third_party/7zip/CPP/7zip/UI/FileManager/App.cpp)。

### 2.7 校验功能

CRC 子菜单完整集合为 CRC-32、CRC-64、XXH64、MD5、SHA-1、SHA-256、SHA-384、SHA-512、SHA3-256、BLAKE2sp、`*` 全部算法。[MyLoadMenu.cpp:763](../third_party/7zip/CPP/7zip/UI/FileManager/MyLoadMenu.cpp#L763) 逐个映射到引擎算法名。

计算选中的文件与目录后，结果包含文件数、目录数、总大小、文件数据摘要、数据与名称摘要；有备用流时包含其统计及摘要。[HashGUI.cpp:205](../third_party/7zip/CPP/7zip/UI/GUI/HashGUI.cpp#L205) 和 [PanelCrc.cpp:162](../third_party/7zip/CPP/7zip/UI/FileManager/PanelCrc.cpp#L162) 定义结果流程。结果可复制文本。

CRC SHA 上下文菜单另外支持生成 `.sha256` 清单和“测试压缩包：校验和”。清单可作为 provider 打开、显示条目并验证。用户主动发起的计算、生成与验证是产品功能，不为一般文件浏览自动生成摘要文件。

## 3. 独立窗口与对话框

| 窗口 / Qt 类 | 入口 | 完整信息结构与按钮 | 源码 |
| --- | --- | --- | --- |
| 添加到压缩包 / `AddArchiveDialog` | 添加工具栏、7-Zip 菜单、拖入归档 | 顶部归档路径与历史、浏览；左侧格式、级别、方法、字典、单词/阶数、固实块、线程/硬件线程、压缩内存预算/估计、解压内存估计、分卷、自由参数、高级选项；右侧更新模式、路径模式、SFX、压缩共享文件、成功后删除源文件、密码/重复密码/显示、加密方法、加密文件名；确定、取消、帮助 | [CompressDialog.rc:58](../third_party/7zip/CPP/7zip/UI/GUI/CompressDialog.rc#L58) |
| 添加高级选项 / `ArchiveOptionsDialog` | 添加窗口“选项” | 链接/元数据：符号链接、硬链接、备用流、安全信息；时间：指定精度、是否保存修改/创建/访问时间、归档时间取最新成员时间、保持源文件访问时间；默认/明确设置状态；确定、取消、帮助 | [CompressOptionsDialog.rc:21](../third_party/7zip/CPP/7zip/UI/GUI/CompressOptionsDialog.rc#L21) |
| 提取 / `ExtractDialog` | 提取工具栏、提取文件… | 目标路径历史与浏览；可启用输出子目录名称；路径模式；消除重复根目录；覆盖模式；密码和显示；恢复安全信息；确定、取消、帮助 | [ExtractDialog.rc:28](../third_party/7zip/CPP/7zip/UI/GUI/ExtractDialog.rc#L28) |
| 操作进度 / `OperationDialog` | 压缩、提取、测试、复制、移动、删除、哈希、分割、合并 | 已用时间、剩余时间、处理文件/总文件、错误数、总大小、速度、已处理、压缩后大小、压缩率；操作状态、当前文件、进度条、诊断消息列表；后台/前台、暂停/继续、取消；结束后结果/关闭 | [ProgressDialog2a.rc:43](../third_party/7zip/CPP/7zip/UI/FileManager/ProgressDialog2a.rc#L43) |
| 密码 / `PasswordDialog` | 读取加密头或加密成员 | 归档上下文、密码输入、显示密码、确定、取消；错误密码可重试 | [PasswordDialog.rc:11](../third_party/7zip/CPP/7zip/UI/FileManager/PasswordDialog.rc#L11) |
| 替换冲突 / `OverwriteDialog` | 提取/复制时目标已存在 | 原文件与新文件名称、图标、大小、时间；是、全部是、自动重命名、否、全部否、取消 | [OverwriteDialog.rc:21](../third_party/7zip/CPP/7zip/UI/FileManager/OverwriteDialog.rc#L21) |
| 内存请求 / `MemoryRequestDialog` | 引擎解压内存需求超过用户设置 | 需求说明；后续操作允许上限复选与 GB 数值；允许解压/跳过归档；当前操作记住选择；继续、取消 | [MemDialog.rc:16](../third_party/7zip/CPP/7zip/UI/FileManager/MemDialog.rc#L16) |
| 诊断消息 / `MessagesDialog` | 操作或打开产生警告/错误 | 可滚动的诊断列表；对象路径和具体错误；关闭；复制选中消息 | [MessagesDialog.rc:7](../third_party/7zip/CPP/7zip/UI/FileManager/MessagesDialog.rc#L7) |
| 属性 / `PropertiesDialog` | 信息工具栏、属性、临时文件属性 | 名称/值列表；单文件完整属性、原始属性；多选文件和目录数、大小、压缩大小汇总；当前目录与全部归档层次；格式错误与警告、偏移、物理大小、尾部大小等；关闭、复制 | [PanelMenu.cpp:158](../third_party/7zip/CPP/7zip/UI/FileManager/PanelMenu.cpp#L158)、[PanelMenu.cpp:172](../third_party/7zip/CPP/7zip/UI/FileManager/PanelMenu.cpp#L172) |
| 复制 / 移动 / 合并目标 / `CopyMoveDialog` | F5/F6、合并文件 | 明确操作标题与目标提示；可编辑目标路径历史、浏览；源项目摘要；确定、取消；双面板预填另一面板路径 | [CopyDialog.rc:9](../third_party/7zip/CPP/7zip/UI/FileManager/CopyDialog.rc#L9)、[PanelSplitFile.cpp](../third_party/7zip/CPP/7zip/UI/FileManager/PanelSplitFile.cpp) |
| 分割文件 / `SplitDialog` | 文件 → 分割 | 目标路径历史、浏览、分卷字节数预设和自定义值；确定、取消；提交前展示分卷数量 | [SplitDialog.rc:7](../third_party/7zip/CPP/7zip/UI/FileManager/SplitDialog.rc#L7)、[PanelSplitFile.cpp](../third_party/7zip/CPP/7zip/UI/FileManager/PanelSplitFile.cpp) |
| 创建链接 / `LinkDialog` | 文件 → 链接 | 链接路径、目标路径、两个浏览入口、当前目标说明；类型选择；创建链接、取消 | [LinkDialog.rc:10](../third_party/7zip/CPP/7zip/UI/FileManager/LinkDialog.rc#L10) |
| 文件夹/文件名称 / `NameDialog` | 新建文件夹、新建文件 | 操作名称、名称输入，默认新建名称；确定、取消；成功后选中新条目 | [PanelOperations.cpp:363](../third_party/7zip/CPP/7zip/UI/FileManager/PanelOperations.cpp#L363)、[PanelOperations.cpp:426](../third_party/7zip/CPP/7zip/UI/FileManager/PanelOperations.cpp#L426) |
| 注释 / `CommentDialog` | 文件 → 注释 | 标题包含目标名；已有注释输入；确定、取消 | [PanelOperations.cpp:487](../third_party/7zip/CPP/7zip/UI/FileManager/PanelOperations.cpp#L487) |
| 通配符选择 / `SelectionDialog` | 编辑 → 选择/取消选择 | 对应操作标题；通配符输入，默认 `*`；确定、取消 | [PanelSelect.cpp:154](../third_party/7zip/CPP/7zip/UI/FileManager/PanelSelect.cpp#L154) |
| 文件夹历史 / `HistoryDialog` | 查看 → 历史 | 历史路径列表、当前焦点、删除历史项、确定跳转、取消；归档路径可重入 | [PanelFolderChange.cpp:866](../third_party/7zip/CPP/7zip/UI/FileManager/PanelFolderChange.cpp#L866) |
| 目录选择 / 原生 `QFileDialog` | 路径旁浏览按钮 | 文件系统位置、上一级、新建目录、路径输入、文件/目录过滤、确认与取消；选择普通磁盘路径 | [BrowseDialog.rc](../third_party/7zip/CPP/7zip/UI/FileManager/BrowseDialog.rc)、[BrowseDialog.cpp:873](../third_party/7zip/CPP/7zip/UI/FileManager/BrowseDialog.cpp#L873) |
| 临时文件 / `TempFilesDialog` | 工具 → 删除临时文件 | 删除、刷新、上级、只读当前路径、详细列表、过滤器、右键属性、关闭、帮助；显示工作临时目录内容 | [BrowseDialog2.rc:12](../third_party/7zip/CPP/7zip/UI/FileManager/BrowseDialog2.rc#L12)、[BrowseDialog2.cpp:1853](../third_party/7zip/CPP/7zip/UI/FileManager/BrowseDialog2.cpp#L1853) |
| 哈希结果 / `HashResultDialog` | CRC / CRC SHA | 算法摘要、对象数、字节数、数据与名称结果、错误；可复制；关闭 | [HashGUI.cpp:310](../third_party/7zip/CPP/7zip/UI/GUI/HashGUI.cpp#L310) |
| 基准测试 / `BenchmarkDialog` | 工具 → 基准测试 | 字典、CPU 线程、硬件线程、内存；压缩/解压各含当前与结果行，大小、CPU 使用率、速度、评级/使用率、评级；总评级、用时、轮数、CPU/指令集/系统/版本、日志；重启、停止、帮助、关闭 | [BenchmarkDialog.rc:66](../third_party/7zip/CPP/7zip/UI/GUI/BenchmarkDialog.rc#L66) |
| 选项 / `PreferencesDialog` | 工具 → 选项 | 六页结构见下一节；确定、取消、应用；语言改变后更新菜单、工具栏、属性名 | [OptionsDialog.cpp:31](../third_party/7zip/CPP/7zip/UI/FileManager/OptionsDialog.cpp#L31) |
| 帮助 / `HelpWindow` | 帮助内容、对话框帮助 | 左侧目录/索引/查找，右侧命令和参数说明，当前对话框定位帮助主题 | [HelpUtils.cpp](../third_party/7zip/CPP/7zip/UI/FileManager/HelpUtils.cpp) |
| 关于 / `AboutDialog` | 关于 | 应用版本、引擎版本、版权、开源许可证和项目链接；关闭 | [AboutDialog.rc](../third_party/7zip/CPP/7zip/UI/FileManager/AboutDialog.rc) |

操作确认还包括本地/归档删除、归档内文件写回、写入归档、分割卷数和正在执行的操作取消。这些确认采用 `QMessageBox` 或文案明确的 `QDialog`，保留对象路径、操作目的和选择结果。外部编辑会话的写回必须定位原始归档成员；用户在另一面板改变目录不会改变写回目标（[PanelItemOpen.cpp:1786](../third_party/7zip/CPP/7zip/UI/FileManager/PanelItemOpen.cpp#L1786)）。

### 3.1 添加与提取的行为模型

添加窗口直接展示格式相关参数。格式列表由引擎可写能力生成；选择格式后再生成方法、字典、固实、线程、加密等允许选项。格式识别目录和可创建归档下拉分别表达读取与写入能力。单文件压缩流与包含目录树的容器，在选择范围与输出名称上明确区分。

更新模式保持原版添加并替换、更新、刷新既有文件、同步等语义；路径模式保持相对、完整和绝对路径语义。自由参数传给同一参数解析层，高级选项和简单控件生成的属性合并后交给引擎。修改参数后即时更新内存估计与可用性。

提取窗口的路径模式、覆盖模式、密码、输出子目录、根目录消重在同一页。覆盖策略必须区分询问、直接覆盖、跳过、自动改名提取文件、自动改名现有文件。测试与提取使用相同完整性错误分类，测试不创建输出成员。

### 3.2 设置六页

| 页面 | 原版控件集合 | M7Arc 对应 |
| --- | --- | --- |
| 系统 | 文件扩展名关联列表；当前用户/所有用户关联状态与批量动作 | 保留格式关联表；按引擎格式与扩展名生成；显示 macOS 当前默认应用及系统支持的更改入口。原版来源：[SystemPage.rc:7](../third_party/7zip/CPP/7zip/UI/FileManager/SystemPage.rc#L7) |
| 7-Zip 集成 | 集成右键菜单、32 位集成、级联菜单、菜单图标、消除重复根目录、Zone.Id 传播、逐项启用上下文动作 | macOS 页命名“Finder 集成”；配置压缩、提取、测试、校验、快速压缩和邮件草稿动作；级联与条目开关仍服务应用内菜单。原版来源：[MenuPage2.rc:7](../third_party/7zip/CPP/7zip/UI/FileManager/MenuPage2.rc#L7) |
| 文件夹 | 工作目录：系统临时目录/当前目录/指定目录；指定路径和浏览；仅对可移动驱动器使用 | 三种工作目录原样保留；外置卷使用开关；临时文件窗口入口。原版来源：[FoldersPage2.rc:1](../third_party/7zip/CPP/7zip/UI/FileManager/FoldersPage2.rc#L1) |
| 编辑器 | 查看器、编辑器、差异工具，各带路径输入与浏览 | 支持选择 `.app` 或可执行程序并保存参数；归档内修改写回会话共用。原版来源：[EditPage2.rc:1](../third_party/7zip/CPP/7zip/UI/FileManager/EditPage2.rc#L1) |
| 设置 | 显示 `..`、真实图标、整行选择、网格线、单击打开、另一种选择模式、显示系统菜单、大内存页、解压内存上限 | 文件视图选项与解压内存上限保留；按 macOS 平台能力处理内存页及系统菜单。原版来源：[SettingsPage2.rc:8](../third_party/7zip/CPP/7zip/UI/FileManager/SettingsPage2.rc#L8) |
| 语言 | 语言选择和语言信息 | `QTranslator`；中文/英文及后续翻译，改变语言时刷新所有菜单与属性标题。原版来源：[LangPage.rc:9](../third_party/7zip/CPP/7zip/UI/FileManager/LangPage.rc#L9) |

## 4. Qt 模块落点

当前实现：

```text
apps/desktop/src/ui/
  MainWindow.{h,cpp}            # 菜单、工具栏、面板、命令注册，前端对 JobRunner 的询问实现
  FilePane.{h,cpp}              # 地址栏、列表、状态栏
  ArchiveModel.{h,cpp}          # 归档条目与属性列的表模型
  ArchiveDialogs.{h,cpp}        # 添加、提取、覆盖、密码、属性、设置等对话框
  OperationController.{h,cpp}   # 进度窗口、暂停与取消
  HashDialog / BenchmarkDialog / SfxDialog / FileMetadataDialog / HelpDialog
  SystemClipboard / ToolbarIcons
apps/desktop/src/app/
  Workspace / JobRunner / Transfer / LocalTransfer / EditSession / TempStore
  CommandRegistry / PaneState / ArchiveRows / FileSplit / FormatCatalog / Settings / Translation
apps/desktop/src/platform/
  services/MacServices.mm       # Finder 服务
  drag/MacFilePromises.mm       # 文件 promise 拖出
  quicklook/                    # Quick Look 预览扩展
src/platform/MacMetadata.*      # 元数据采集与恢复（引擎共用）
```

`CommandRegistry` 注册命令标识与回调，菜单、工具栏和快捷键只引用标识。面板与对话框收集参数，`Workspace` 准备引擎作业并交给 `JobRunner`；作业可暂停、取消，并通过前端请求密码、覆盖和内存决策。引擎工作线程通过 Qt queued 调用通知主线程，UI 不直接操作 Win32 控件或 Registry。

`ArchiveModel` 保存条目索引和属性，不把归档目录转成磁盘目录；嵌套归档的外层栈由面板与 `Workspace` 维护。类型对应的视图设置进入 `QSettings`；格式、压缩方法、摘要算法与能力来自引擎目录。

计划中的拆分：把对话框、面板和设置页拆成 `dialogs/`、`panels/`、`preferences/` 子目录，增加 `CommandState`（按焦点、选择和 provider 能力计算命令可用性）、`FolderItemModel` 与排序代理、`PathBar`/`PanelStatusBar` 独立控件，以及 `FileAssociations`、`TrashService` 等 macOS 服务封装；当前这些职责分别在 `MainWindow`、`FilePane` 和 `MacServices` 中。

原版依赖关系在 [FM.mak:13](../third_party/7zip/CPP/7zip/UI/FileManager/FM.mak#L13) 列出：App、Panel 系列、FSFolder/FSFolderCopy、RootFolder、FSDrives、NetFolder、AltStreamsFolder、Agent/ArchiveFolder、各对话框、回调与视图设置。迁移保持这些职责边界，Win32 窗口层由 Qt 重写，provider、归档算法与操作语义由平台适配后的核心承担。

## 5. 平台差异与已知边界

Windows Shell 的 32 位/64 位 COM 注册、所有用户 Registry 关联、Zone.Identifier、NTFS 安全描述符、NTFS 备用流、Directory Junction、WSL 重解析链接和 Windows 大内存页都是原版可达功能。macOS 端分别记录其底层能力与呈现方式，不能把全部控制项显示成可直接生效的本地功能。

本地链接创建提供硬链接和符号链接；归档中的 NTFS/WSL 链接属性继续读取、展示并按归档格式处理。备用数据流保留为归档/映像 provider 的可浏览能力；macOS 扩展属性、资源分支和 ACL 使用各自平台模型。恢复或存储这些属性是否支持，由格式与平台两侧共同决定。

SFX 参数必须指定目标平台：官方 Windows SFX 模块生成 Windows 可执行产物，macOS 原生自解压入口需要独立实现及分发设计。界面显示目标平台与模块类型，不把 Windows `.exe` 标为 macOS 自解压应用。

原版外部编辑依赖进程与临时文件会话；macOS 编辑器可能复用已运行进程，因此 M7Arc 以文件修改监测配合明确“更新归档”动作完成写回。邮件动作完成压缩后打开系统邮件草稿，由用户在邮件应用发送。

另有一组条件命令：`Ver Edit / Ver Commit / Ver Revert / Ver Diff` 仅在已配置 Diff 和版本控制命令、选中一个满足条件的本地文件时追加；只读状态决定显示哪些动作（[MyLoadMenu.cpp:572](../third_party/7zip/CPP/7zip/UI/FileManager/MyLoadMenu.cpp#L572)、[MyLoadMenu.cpp:697](../third_party/7zip/CPP/7zip/UI/FileManager/MyLoadMenu.cpp#L697)）。迁移保留外部版本控制命令配置与条件分发，其入口不占用普通默认菜单。

当前 Windows 桌面资源不显示 `Benchmark 2`，其资源声明在 `UNDER_CE` 条件内；“显示 NTFS 流”“显示已删除文件”查看菜单资源被注释；“恢复备用流”提取复选框资源也被注释。实际仍可用的备用流打开动作、provider 元数据与引擎处理能力分别保留，不能依据一条被注释的控件宣称完整桌面入口存在。
