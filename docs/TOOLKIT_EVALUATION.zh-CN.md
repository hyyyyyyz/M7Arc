# 桌面端工具包评估：Qt 是否太大、能否换掉

测量环境：Apple Silicon、macOS 27.3、10 核；Qt 6.11.2（官方 `clang_64` SDK）；M7Arc 提交 `bf63e82`；7-Zip 26.03。除注明为估计的项目外，所有数字均为本机实测。原型与取数脚本在 `spikes/`，复现命令见文末。

## 结论

1. **Qt 的体积问题主要来自打包，不来自 Qt 本身。** 官方 Qt SDK 的框架是 arm64 + x86_64 通用二进制，而 `packaging/macos/package.py` 只校验架构「包含」目标架构，不裁剪多余切片。arm64 的 DMG 因此带着一份用不到的 x86_64 Qt。部署后对每个 Mach-O 做 `lipo -thin`，`.app` 从 59.2 MB 降到 33.7 MB，DMG 从 28.7 MB 降到 16.7 MB（−42%），界面零改动。
2. **不建议静态链接或自编 Qt。** 在 thin 之上它只再省约 2 MB DMG，且前提是放弃 `M7ArcExtractor` 的 Qt 界面（否则每个可执行文件各带一份 Qt，DMG 反而增大到 22.9 MB）；同时静态链接与 `docs/BUILDING.md` 中「用户可替换 Qt 库」的 LGPL 合规方式冲突。
3. **推荐：现在只做 thin；不换工具包。** 换工具包的收益是把 `.app` 压到约 10–11 MB，代价是重写约 5,700 行界面代码（估计 8–12 人周）。这是产品取舍，不是体积问题的唯一解法。
4. **如果所有者仍要换，选原生 AppKit（Swift），不选 Tauri，不选 Slint。** 三个原型里 AppKit 体积最小、窗口出现最快、拖放/服务/表格全部原生，并且 Core 会话交付的 C 接口可被 Swift 直接导入。换的时机在 UI 会话完成 `MainWindow` 拆分之后（见「迁移步骤」）。

## 一、现状实测

### 构建与打包

`cmake --preset release`（Qt 在 `.qt/6.11.2/macos`）后 `cmake --build` 全量构建，`ctest --preset release` 15/15 通过，再运行 `python3 packaging/macos/package.py --qt-prefix .qt/6.11.2/macos`（约 19 s）。产物为 arm64。

| 产物 | 大小 |
| --- | ---: |
| `M7Arc-0.1.0-alpha.2-macos-arm64.dmg` | 28.68 MB |
| 部署后的 `M7Arc.app` | 59.16 MB |

### `.app` 内部构成

| 部分 | 大小 | 占比 |
| --- | ---: | ---: |
| Qt 框架（Core 12.66、Gui 17.83、Widgets 12.69、DBus 1.43、Concurrent 0.11） | 44.72 MB | 75.6% |
| Qt 插件（`platforms/libqcocoa` 1.96、`imageformats` 1.42、`styles/libqmacstyle` 0.51） | 3.90 MB | 6.6% |
| 引擎 `libm7arc_engine.dylib` | 2.91 MB | 4.9% |
| `7zz` 2.97 + `7zCon.sfx` 0.60 + Windows SFX 0.57 | 4.14 MB | 7.0% |
| `M7Arc` 主程序 | 1.15 MB | 1.9% |
| `M7ArcExtractor` | 0.34 MB | 0.6% |
| Quick Look 扩展 | 0.10 MB | 0.2% |
| 许可证文本 0.65 + 其他资源（图标 1.2 等） | 1.90 MB | 3.2% |

Qt 框架与插件合计 48.6 MB，占 82%。Qt 的翻译（`qtbase_zh_CN.qm`、`m7arc_en.qm`）编进主程序的资源，不单独占文件。框架里 10 个 Mach-O 同时含 arm64 与 x86_64 切片。

### 冷启动

用 `spikes/tools/launch_time.swift` 轮询系统窗口列表，量从 `exec` 到进程拥有可见窗口的时间（无参数启动，5–8 次）：

| 场景 | 到窗口出现 | 常驻内存 |
| --- | ---: | ---: |
| 已部署 `M7Arc`，文件缓存已热（中位数） | 326–355 ms | 163 MB |
| 复制后第一次启动 | 1.2–1.7 s | 163 MB |

### 现有界面用到的 Qt 能力

自有代码约 9,400 行，其中界面相关（`apps/desktop/src/ui`、`apps/desktop/src/app`、`apps/desktop/src/extractor`、`apps/desktop/src/main.cpp`）5,714 行。

| 能力 | 用法与规模 | 换工具包时的含义 |
| --- | --- | --- |
| 主窗口、菜单、工具栏 | `QMainWindow`，7 个顶层菜单、25 处快捷键，统一标题栏工具栏；工具栏图标是自绘 `QIconEngine`（139 行） | AppKit 有对应的 `NSToolbar`/`NSMenu`；图标可换 SF Symbols 或保留自绘 |
| 双栏文件表格 | `FilePane`（448 行）：`QTreeView` + `QListView` 图标视图，在 `QFileSystemModel`（本地）和 `ArchiveModel`（`QAbstractTableModel`，187 行）间切换，嵌套归档栈、排序、多选、地址栏 | 本地目录需自建模型（`NSTableView` + `FileManager`/FSEvents）；归档模型逻辑可移植 |
| 对话框 | `CreateDialog`、`ExtractDialog`、设置、`BenchmarkDialog`、`HelpDialog`、`FileMetadataDialog`、`HashDialog`、`SfxDialog` 共 8 个，另有 `QMessageBox` 82 处、`QInputDialog` 14 处、`QFileDialog` 23 处、`QDialogButtonBox` 79 处 | 重做量集中在这里：`ArchiveDialogs.cpp` 875 行、`MainWindow.cpp` 1,905 行 |
| 拖放 | 内部拖放用 `QMimeData`（`application/x-m7arc-members`）；拖出到 Finder 用 `apps/desktop/src/platform/drag/MacFilePromises.mm` 的 `NSFilePromiseProvider`（194 行）。Qt 仅提供 `QString` 与 `Qt::QueuedConnection` | 文件承诺本来就是 AppKit 代码，原生化后更简单 |
| Services / Quick Look | `MacServices.mm`（110 行）、`PreviewController.mm`（121 行）。Quick Look 扩展不链接 Qt | 与工具包无关，原样保留 |
| 国际化 | `tr()` 约 508 处，`m7arc_en.ts` 含 572 条消息；中文是源语言，英文走 `QTranslator`；另有 `HelpDialog` 自带选择函数和双份 `formats.json` | 需转换到 `.strings`/`.xcstrings` 或 JS 词表 |
| 设置 | `QSettings`，约 16 个键，字面量分散 | macOS 上 `QSettings` 落在 CFPreferences，可用 `UserDefaults` 读同一份域 |
| 并发 | `QtConcurrent::run` + `QFutureWatcher`（`OperationController`），3 处 `BlockingQueuedConnection` 回主线程询问密码/覆盖/内存 | 任何目标工具包都要重写成该工具包的主线程调度 |
| 其他 Qt 基础类 | `QString` 398 处、`QDir` 99、`QFileInfo` 94、`QFile` 48、`QProcess` 11、`QTemporaryDir` 21、`QJson*` 24、`QRegularExpression` 16 | `apps/desktop/src/app/EditSession` 与 `apps/desktop/src/tools` 也依赖，换工具包时要一并处理 |

## 二、候选对比

体积栏中，「实测」来自本次构建；「项目估计」是用实测的非 Qt 部分（引擎 2.91 + `7zz`/SFX 4.14 + 提取器 0.34 + Quick Look 0.09 + 许可证与资源 1.90 = 9.38 MB）加上该候选的运行库得到的推算值。

| 候选 | `.app` | DMG | 界面能原样做到的 | 做不到或要变通的 | 衔接引擎/Rust 核心 | 全部界面重做量 | 语言数 |
| --- | ---: | ---: | --- | --- | --- | --- | --- |
| **A. Qt 现状** | 59.2 MB（实测） | 28.7 MB（实测） | 全部 | — | C++ 直接调用 | 0 | C++、ObjC++ |
| **B. Qt + thin（`lipo -thin`）** | 33.7 MB（实测） | 16.7 MB（实测） | 全部 | — | 同 A | 0，改 `package.py` 几行 | 同 A |
| **B′. thin + 去 `imageformats` + `strip`** | 33.0 MB（实测） | 16.4 MB（实测） | 全部（图标均为 PNG，冒烟与窗口启动正常） | — | 同 A | 0 | 同 A |
| **C. 静态精简 Qt**（LTO、`-dead_strip`，去 DBus/Network/Sql/Xml/OpenGL/PrintSupport/GIF/ICO/JPEG） | 27.1 MB（无提取器）／44.1 MB（提取器同样静态链接）（实测） | 14.7 MB／22.9 MB（实测） | 全部，窗口正常启动 | LGPL 要求提供可重链接的目标文件，与 `BUILDING.md` 的「替换库」说明冲突；自编 Qt 要自己维护和升级 | 同 A | 0，但要维护 Qt 编译（本机 10 核约 5 分钟） | 同 A |
| **D. AppKit（Swift）** | 约 10–11 MB（项目估计） | 约 5–6 MB（估计） | 原生表格（原型 11 万行流畅）、`NSToolbar` 统一标题栏、菜单、文件承诺拖出、Services、Quick Look、VoiceOver | 本地文件表要自建；8 个对话框全部重做；没有 Interface Builder 也能做（`swiftc` 可直接构建） | Core 的 C 接口可被 Swift 直接导入；当前 C++ 头文件需桥接层 | 估计 8–12 人周 | 加 Swift |
| **E. Tauri 2（Rust + WKWebView）** | 约 13 MB + 系统 WebKit 的 3 个辅助进程（估计） | 约 7 MB（估计） | `design/` 里的 HTML 原型可复用；Rust 后端直接用 `m7arc-core`；系统菜单可用 | 无 `NSToolbar`；文件承诺拖出要从 WebView 的拖拽事件进入原生拖拽会话，需 `objc2` 手写，风险最高；虚拟滚动表格要自己实现选择、键盘、列宽、无障碍；额外约 75 MB 的 WebKit 进程内存 | Rust 直接调用 | 估计 5–8 人周，但拖放与表格行为要另外补 | 加 JS/HTML/CSS |
| **F. Slint（纯 Rust）** | 约 17 MB（估计） | 约 11 MB（估计） | 惰性行模型可承载 11 万行；`@tr` 支持 gettext | 控件不是原生控件；无 `NSToolbar`；winit 没有拖出文件的接口，文件承诺要从原生视图句柄手写；无障碍弱于 AppKit | Rust 直接调用 | 估计 8–12 人周 | 加 `.slint` DSL |

「重做量」的估计依据：界面代码 5,714 行、8 个对话框、`MainWindow` 1,905 行的命令分发、572 条文案。没有按候选实际做过完整界面，不能当作承诺。

### 候选取舍说明

- **B 与 C 的差别很小**：C 的主程序 18.1 MB 对应 B 里 Qt 框架 + 插件 + 主程序的 22.4 MB，只省 4.3 MB，DMG 小约 2 MB；而提取器若也静态链接，会再带一份 17 MB 的 Qt。想吃下 C 的收益，必须先把提取器改成不依赖 Qt，那已经是换工具包的一部分工作。
- **Tauri 的主要风险是行为而不是体积**：体积和启动速度都好（原型主程序 3.2 MB），但这个应用的核心体验是文件表格和拖拽，这两项在 WebView 里都是自己补。
- **Slint 在体积上没有优势**：原型主程序 7.5 MB，比 AppKit 大 60 倍；控件不原生，也无拖出支持。

## 三、原型实测

三个原型加一个 Qt 对照物，都是「左栏本地目录 + 右栏归档表格」的双栏窗口，条目来自 `libm7arc_engine.dylib`（通过 `spikes/common/m7arc_c.*` 的薄 C 桥接）。测试归档为 110,200 个条目（10 万个小文件加目录）的 `.7z`，由 `spikes/tools/make_big_archive.sh` 生成。每项 5 次。

| | Qt 对照 | AppKit | Tauri | Slint |
| --- | ---: | ---: | ---: | ---: |
| `.app` 大小（含引擎 dylib 3.27 MB） | —（未部署） | **3.39 MB** | 6.44 MB | 10.79 MB |
| DMG 大小 | — | **1.54 MB** | 2.95 MB | 6.09 MB |
| 主程序大小 | — | 0.12 MB | 3.17 MB | 7.52 MB |
| 无归档时窗口出现（中位数） | 291 ms | **222 ms** | 225 ms | 335 ms |
| 无归档时常驻内存 | 159 MB | 98 MB | 102 MB（另有 WebKit 辅助进程约 75 MB） | 104 MB |
| 引擎读取 11 万条目 | 360 ms | 358 ms | 362 ms | 361 ms |
| 进程启动到首批行绘制完成（中位数） | **539 ms** | 641 ms | 748 ms | 648 ms |
| 按大小排序 11 万行 | 48 ms | 46 ms | 16 ms（Rust 排序 + 重绘可见窗口） | 5 ms（惰性模型，不含重排版） |
| 打开归档后常驻内存（主进程） | 262 MB | 215 MB | 209 MB（另有 WebKit 辅助进程约 75 MB） | 206 MB |

观察：

- **引擎读取 11 万条目的 360 ms 与约 109 MB 的内存（单独调用引擎实测）占了各原型的大头**，与工具包无关；这是 Core 侧的事。
- 十万级表格在 Qt、AppKit、Slint、Tauri（虚拟滚动，按需向 Rust 取行）里都可用；差距在 UI 开销：除引擎读取外，Qt 约 180 ms，AppKit 约 280 ms，Slint 约 290 ms，Tauri 约 390 ms。Qt 反而最快。
- Tauri 窗口在 243 ms 出现，数据约 750 ms 后到位；其余三个等引擎读完才显示窗口（约 650–710 ms）。
- 原型没有实现拖放、Services、选择与键盘行为，这些项的结论依据 API 能力与已有 `src/platform` 代码，不是实测。

## 四、推荐

**现在**：只给 Qt 瘦身，具体是 `packaging/macos/package.py` 在 `macdeployqt` 之后、`verify_runtime` 之前，对每个同时含目标架构与其他切片的 Mach-O 执行 `lipo -thin <arch>`；可选地删掉 `PlugIns/imageformats` 并 `strip -x -S` 框架和插件。预期 DMG 28.7 → 16.4–16.7 MB。这一项属于 Build 会话的 `packaging/**`，本评估不改。

**不做**：静态链接 / 自编 Qt（理由见结论 2）。

**什么时候换**：当 16.7 MB 的 DMG 或每次 3 个 Qt 框架的维护成本仍然不可接受，或所有者想去掉 LGPL 义务时。此时选 AppKit：

- 体积（估计 `.app` 约 10–11 MB）和内存（原型 98 MB 对 159 MB）最优；
- 现有 `src/platform` 已经是 AppKit 代码，拖放、Services、Quick Look 原生化后反而变简单；
- 项目只支持 macOS，没有跨平台包袱；
- Core 的 C 接口让 Swift 无需 C++ 桥接就能使用引擎；
- 代价是多一门语言（Swift）和约 8–12 人周的界面重写。

### 迁移步骤（仅在决定换时）

1. 等 UI 会话把 `MainWindow` 里的应用层（传输、编辑会话、命令分发）拆出，并等 Core 的 C 接口合并。这一步和工具包无关，也是 Qt 路线的收益。
2. 新建 Swift 目标，与 Qt 主程序并存；先做只读浏览（双栏表格、打开归档、列表、排序），复用 `spikes/appkit` 的结构。
3. 加入提取/新建/测试等作业和 8 个对话框；把 `OperationController` 的单槽位作业模型改成 Swift 并发。
4. 迁移拖放与 Services，删去对 Qt 私有 pasteboard 类型名的依赖。
5. 把 `M7ArcExtractor` 改成不依赖 Qt（它与主程序共享 `OperationController.cpp`，这一步要与 UI 会话协调）。
6. 文案从 `m7arc_en.ts` 转成 `.xcstrings`；`QSettings` 域与 `UserDefaults` 对齐，旧设置原样沿用。
7. 全部命令达到对等后删去 Qt 依赖，`package.py` 不再调用 `macdeployqt`。

## 五、限制

- 原型只量了列表、排序和启动，没有实现选择、键盘导航、拖放和 Services，对应结论来自 API 能力与现有代码。
- Tauri 表格是 DOM 虚拟滚动，没有列宽调整与多选，排序数字与其他原型的工作量不完全相同（Qt/AppKit 含全量重排版，Slint/Tauri 只处理可见部分）。
- 「首批行绘制完成」的计时点在各工具包里略有差异（Qt 为 `show()` 后第一个事件循环，AppKit 为下一个主队列回合，Tauri 为 WebView 两帧之后，Slint 为首次渲染通知），数字用于量级对比。
- 静态 Qt 只验证了冒烟测试、窗口出现和启动时间，没有手工走完整个界面。
- 只测了 arm64。x86_64 的 DMG 与 universal2 未测；universal2 的 Qt 部分约为 arm64 的两倍。
- 「项目估计」的体积和 8–12 人周的工作量是推算，不是实测。
- 在本机用 Command Line Tools 构建 Qt 需要 `-DQT_NO_APPLE_SDK_AND_XCODE_CHECK=ON`（没有安装 Xcode）。

## 附：复现

```sh
# 构建与打包基线（见 docs/BUILDING.md）
cmake --preset release && cmake --build --preset release --parallel 8
ctest --preset release
python3 packaging/macos/package.py --qt-prefix .qt/6.11.2/macos

# 部署后各部分占比；窗口出现时间
python3 spikes/tools/measure_bundle.py <M7Arc.app>
swiftc -O spikes/tools/launch_time.swift -o launch_time && ./launch_time <M7Arc.app>/Contents/MacOS/M7Arc 8

# 测试归档与原型，见 spikes/README.md
spikes/tools/make_big_archive.sh build/release/tools/7zz <工作目录> 100000
spikes/appkit/build.sh $PWD $PWD/build/release/lib $PWD/build/spikes/appkit
spikes/tauri/build.sh  $PWD $PWD/build/release/lib $PWD/build/spikes/tauri
<原型>.app/Contents/MacOS/<原型> --open <归档> --report
```
