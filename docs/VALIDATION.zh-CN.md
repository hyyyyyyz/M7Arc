# 验证记录

本文先列出当前代码的测试清单，其后是 0.1.0-alpha.1 的发行验证记录。

## 当前测试清单

`ctest --preset release` 共 21 项，调用真实引擎、文件系统和 Qt 控件。`tools.mac_sfx_bundle` 需要已部署的应用包（`-DM7ARC_SFX_SOURCE_APP=<M7Arc.app>`），未提供时 CTest 报告为跳过，其余 20 项在本机通过。`cargo test --workspace` 另有 39 项单元与集成测试和 1 项文档测试，同样运行真实引擎。

| 测试 | 位置 | 验证内容 |
| --- | --- | --- |
| `engine.integration` | `tests/engine/EngineIntegration.cpp` | 格式注册、归档往返、成员改写、加密、分卷、取消与阶段枚举 |
| `engine.capi` | `tests/engine/CApiTest.c` | 纯 C 程序经 `m7arc_c.h` 调用引擎 |
| `engine.external_modules` | `tests/engine/ExternalModulesTest.cpp` | 外部格式、外部 codec 与内建 codec 互用 |
| `platform.metadata`、`platform.metadata_interop` | `apps/desktop/tests/platform` | xattr、资源叉、FinderInfo、ACL；与系统 ditto 的 ZIP 双向互操作 |
| `platform.services`、`platform.file_promises`、`platform.quicklook_controller`、`platform.quick_actions` | `apps/desktop/tests/platform` | Services 回调、文件 promise、Quick Look 控制器目录读取、Automator 工作流 |
| `tools.sfx_builder`、`tools.windows_sfx`、`tools.mac_sfx_bundle`、`tools.integration` | `apps/desktop/tests/tools` | 控制台与图形 SFX、Windows 三种 stub、官方 `7zz` 与 SFX 模块 |
| `app.settings`、`app.command_registry`、`app.edit_session`、`app.job_runner`、`app.workspace` | `apps/desktop/tests/app` | 设置、命令注册、编辑会话与嵌套回写、作业排队/取消/密码、`Workspace` 的业务流程 |
| `app.password_retry`、`app.create_options` | `apps/desktop/tests/app` | 密码重试流程；七种写入格式与三种 TAR 复合预设经参数模型创建并提取 |
| `app.smoke` | `M7Arc --smoke-test` | 加载引擎与格式目录，构建主窗口（资源、翻译），输出版本行后退出 |
| `cargo test --workspace` | `apps/cli/tests`、`crates/m7arc-core/tests` | 命令行的命令、`--json`、退出码、无终端行为；`m7arc-core` 的创建、列出、测试、提取、编辑、密码与取消 |

`packaging/macos/package.py` 在打包后对部署的应用包再运行 `M7Arc --smoke-test`、`M7ArcExtractor --smoke-test` 和 `Contents/Helpers/m7arc --version`，并检查所有 Mach-O 的架构、部署目标和依赖。

## 0.1.0-alpha.1 发行验证

验证日期：2026-09-06。本地测试环境为 Apple Silicon、macOS 27、Apple Clang 21 与 Qt 6.11.2；GitHub Actions 使用 macOS 15.7.9 的 Apple Silicon 和 Intel runner。应用分别构建 arm64 和 x86_64，最低部署目标均为 macOS 13。本地 x86_64 程序通过 Rosetta 执行，CI 中两个架构均在对应原生架构上执行。

## 自动化集成验证

当时两个架构均通过全部 15 项 CTest 测试。测试调用真实引擎、文件系统和 Qt 控件。

| 范围 | 验证内容 |
| --- | --- |
| 引擎与外部模块 | 内建格式注册、归档往返、成员改写、加密与分卷；外部格式读写、外部 codec 与内建 codec 互用 |
| 压缩参数 | 七种写入格式与三种 TAR 复合预设经 GUI 参数模型创建并提取；字典、线程、固实块与高级参数映射 |
| 密码重试 | 文件名加密与仅数据加密的 7z，错误密码后重试、精确成员选择和取消 |
| 编辑会话 | 归档内编辑、成员身份、嵌套回写与取消后的编辑文件保留 |
| macOS 元数据 | 内容、执行权限、二进制 xattr、资源叉、FinderInfo、标签与 ACL；M7Arc ZIP 和系统 ditto 双向互操作 |
| 系统入口 | Services 真实 pasteboard 回调、文件 promise 输出、Quick Actions 工作流、Quick Look 控制器目录读取 |
| CLI 与 SFX | 官方 7zz、Mac console SFX、图形提取器构建、Windows 三种 stub 与安装配置、实际载荷解包 |
| 应用启动 | Qt 主窗口、资源、翻译和引擎连接 |

提交 `56f8f32` 的 [GitHub Actions 运行](https://github.com/hyyyyyyz/M7Arc/actions/runs/34009884537)通过两个架构的构建、测试和分发验证。DMG 已上传为该次运行的构建产物。

| 原生 CI 环境 | CTest | DMG 打包与部署后启动 | SFX 与磁盘镜像分发验证 |
| --- | --- | --- | --- |
| [macOS 15.7.9 · arm64](https://github.com/hyyyyyyz/M7Arc/actions/runs/34009884537/job/101423751269) | 15 / 15 通过 | 通过 | 通过 |
| [macOS 15.7.9 · x86_64](https://github.com/hyyyyyyz/M7Arc/actions/runs/34009884537/job/101423751406) | 15 / 15 通过 | 通过 | 通过 |

## DMG 与独立运行

两个架构的 DMG 均包含主应用、官方 CLI、共享引擎、Qt 框架及插件、图形提取器、系统扩展和许可文本。打包验证检查所有 Mach-O 的架构、部署目标和可重定位依赖，并通过从内到外的 ad-hoc 签名验证。

去掉 Qt 和 DYLD 开发环境变量后，主应用与图形提取器均能从部署后的目录启动。两个 DMG 均通过完整的 SFX 分发验证：生成内嵌与外置载荷应用、生成并挂载两类 DMG、提取加密载荷，以及验证生成的 Windows GUI、Console 和 Installer 载荷。

图形界面完成包含中文文件名、普通文本与子目录的创建和提取。提取结果与三个源文件逐字节一致。

Apple Silicon DMG 安装副本通过 Finder Quick Look 验证：启用 M7Arc 的快速查看扩展后，Finder 空格预览加载随包的 `M7ArcPreview.appex`，成员表格正确显示 7z 文件中的名称、原始大小、压缩大小和 LZMA2 方法。预览使用 ad-hoc 签名的实际部署组件。

## 大归档

10 万成员 ZIP 的后台目录读取为 494 ms，模型发布为 254 ms。大图标、小图标、列表与详细视图切换的最大主线程间隔分别为 35、22、18、27 ms。完整保留 10 万行与 13 个属性列。测量方式和样本见[大归档记录](LARGE_ARCHIVES.zh-CN.md)。

元数据互操作样本和结果见[元数据互操作记录](METADATA_INTEROP.zh-CN.md)。

## 已知限制

- 当前包没有 Developer ID 签名和 Apple 公证；ad-hoc 签名不认证发布者身份。
- macOS 13 的真实安装和运行、Intel Mac 的桌面交互验收与 Windows SFX 的实际启动尚待对应设备验证。
- 格式目录包含官方完整能力，但测试样本尚未覆盖每种格式及其所有变体。
- 单次本地性能测量用于确认大归档界面可用性，不代表全部设备的性能。
