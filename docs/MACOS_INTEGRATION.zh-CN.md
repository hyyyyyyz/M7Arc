# macOS 系统入口

M7Arc 的 Finder Services、Quick Actions、文件关联和归档成员拖放共用应用的真实归档操作。系统入口把文件路径交给同一组添加、解压、测试窗口，密码、覆盖询问、进度和取消沿用主窗口的任务处理。

## Finder Services

安装应用并启动后，在 Finder 选择文件或目录，从 Services 菜单调用“使用 M7Arc 压缩”“使用 M7Arc 解压”“使用 M7Arc 测试归档”。Services 接收完整多选路径，支持中文名称和包含空格的路径。添加可以接收跨目录输入；解压和测试按所选归档执行。

应用的 `NSServices` 声明与 `NSApplication.servicesProvider` 提供实际系统调用入口。原生 pasteboard 读取的是文件 URL，并在 Qt 主线程把动作交给 `MainWindow::runService`。[Apple Services](https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/SysServices/Articles/properties.html)

## 可安装 Quick Actions

设置中的安装按钮把三个工作流复制到 `~/Library/Services`：

- `M7Arc Compress.workflow`
- `M7Arc Extract.workflow`
- `M7Arc Test.workflow`

安装后可在 Finder 的 Quick Actions 或 Services 菜单使用；如未显示，在系统设置的扩展与 Services 设置中启用。Quick Action 通过系统 `open` 为请求启动应用实例，并传递 `--service add|extract|test -- <路径…>`。多选作为独立命令参数传递，路径中的空格和引号不会被当作命令执行。

重新安装会更新上述三个 M7Arc 工作流。卸载这些入口时，将对应 `.workflow` 从 `~/Library/Services` 移走；用户的其他工作流保持原位。[Apple Automator Services](https://developer.apple.com/library/archive/documentation/LanguagesUtilities/Conceptual/MacAutomationScriptingGuide/MakeaSystem-WideService.html)

## 默认打开应用

设置显示各扩展名当前默认应用的 bundle identifier。用户选择“设为默认”后，应用通过 `UTType` 解析文件类型，并调用 `NSWorkspace` 设置 M7Arc。系统需要用户同意时显示原生提示；拒绝或失败的项目返回具体错误，界面可以重新读取实际关联状态。

设置只作用于用户选中的类型。应用版本升级和普通启动不会批量更改关联。

## 归档成员拖出到 Finder

拖动归档成员时，应用使用 `NSFilePromiseProvider` 提供文件名、类型和拖动图像。Finder 接受放置并请求实际目标 URL 后，应用才开始解压。未接受的拖动不会产生解压文件。[Apple File Promise Provider](https://developer.apple.com/documentation/appkit/nsfilepromiseprovider)

每个接受的成员在目标目录的同一卷内建立临时工作目录，按所选 index 提取，在完成后重命名到 Finder 指定的目标。目录成员包含选择范围内的子项，Finder 重命名同名文件后提供的新目标也能使用。失败或取消时清理该临时目录。

多项承诺按顺序进入原有任务 worker，保留密码询问、进度和取消。归档面板之间的拖动同时携带 `application/x-m7arc-members` 数据，继续使用应用内的文件传输流程。

## Finder Quick Look 归档预览

`M7ArcPreview.appex` 是独立的原生 Quick Look preview extension，使用 AppKit 表格与 7-Zip 引擎。它在后台读取归档目录，显示成员名、原始大小、压缩大小和编码方法；超过 5,000 项时显示总数及前 5,000 项。预览完成前释放引擎与文件句柄，不提取成员到磁盘。[Apple Quick Look controller](https://developer.apple.com/documentation/QuickLookUI/QLPreviewingController/preparePreviewOfFile(at:completionHandler:))

预览关联覆盖常用归档、压缩流、文件系统和磁盘镜像，声明由格式目录生成。需要密码或缺失分卷时，预览提示用 M7Arc 打开。扩展使用独立 App Sandbox 和用户文件只读 entitlement，随应用放在 `Contents/PlugIns`。

## 工程接线

根 CMake 引入 `cmake/macos.cmake`，对 Qt 应用调用 `m7arc_add_macos_integrations(M7Arc)`。应用 Info.plist 插入 `${M7ARC_SERVICES_PLIST}` 与 `${M7ARC_IMPORTED_ARCHIVE_TYPES_PLIST}`。扩展在所有资源固定后独立签名，再签名外层应用。

| 接口 | 职责 |
| --- | --- |
| `installArchiveServices(handler)` | 注册原生 Services，回调 action 与完整多选路径 |
| `defaultAppForExtension(extension)` | 读取系统当前默认应用 |
| `setDefaultForExtensions(extensions)` | 应用用户指定的默认关联 |
| `installQuickActions()` | 将随包工作流安装到用户 Services 目录 |
| `beginArchivePromiseDrag(...)` | 开始原生文件承诺拖放，延迟请求内容 |
| `writeArchivePromise(...)` | 在引擎 worker 完成指定承诺的提取与交付 |
| `m7arc_quicklook` | 构建无 Qt 依赖的 Quick Look `.appex` |

## 验证范围与已知限制

原生 pasteboard 多选回调、Automator 工作流加载、Quick Look controller 的实际归档读取、拖放交付函数的文件/目录重命名和取消清理都有本地执行验证。Finder 菜单可见性、扩展宿主加载和用户交互提示另需在安装后的系统环境中验证；控制器测试不代表系统已启用扩展。

Ad-hoc 签名不提供 Developer ID 身份或 Apple 公证。系统是否启用 Quick Look 扩展取决于安装、签名和用户扩展设置，发布说明记录实际宿主验证结果。应用本身的归档浏览、压缩和提取不依赖 Quick Look 注册成功。

多卷和加密归档的完整操作在主应用中处理；Finder 预览不显示密码输入窗口。跨架构包分别包含对应架构的主应用、引擎与扩展。
