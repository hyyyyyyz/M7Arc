<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="resources/brand/wordmark-dark.svg">
    <img src="resources/brand/wordmark-light.svg" alt="M7Arc" width="336">
  </picture>
</p>

# M7Arc

M7Arc 是基于官方 7-Zip 26.03 完整引擎的 macOS 归档与文件管理器，使用 C++20 和 Qt Widgets。它保留 7-Zip File Manager 的六组菜单、七按钮工具栏、地址栏和单/双面板，并采用 macOS 系统字体、轮廓图标、圆角控件与浅色/深色界面。

M7Arc 目前是工作名：M 代表 Mac，7 代表 7-Zip，Arc 来自 archive。正式产品名称尚待确定。

## 功能

- 完整内建目录：61 个 handler、25 个 codec、10 个 hasher。RAR/RAR5、磁盘镜像、文件系统和文档容器保留读取入口。
- 创建和更新 7z、ZIP、gzip、bzip2、xz、TAR、WIM，以及 tar.gz、tar.bz2、tar.xz。支持密码、文件名加密、分卷、方法属性、固实块、包含/排除规则、路径及时间选项。
- 归档浏览、选择提取、完整性测试、覆盖处理、密码重试、进度、暂停和取消。删除、重命名、ZIP 成员注释和归档内新建目录按实际格式能力启用。
- 双面板和四类文件传输、四种列表视图、动态属性列、收藏、选择掩码、外部查看/编辑/Diff，以及嵌套归档的逐层回写。
- macOS 扩展属性、资源叉、Finder 标签、ACL 和创建时间的保存与恢复；本地文件操作、链接、废纸篓、挂载卷、Finder 文件拖出及复制粘贴。
- Services、可安装的 Quick Actions、归档文件关联和 Quick Look 预览扩展。
- 多算法摘要、摘要清单、性能测试、文件分割/合并，以及 macOS 终端、macOS 图形、Windows 普通自解压和 Windows 安装器分发。
- 原生外部 Codecs/Formats 模块，包含实际加载状态、错误报告和可构建的模块示例。
- 简体中文和英文界面，包含搜索和快捷键说明的离线帮助。

GUI 通过结构化 C++ API 直接调用引擎。命令行程序 `m7arc` 用 Rust 编写，经引擎的 C 接口使用同一套 7-Zip 代码，提供创建、列出、提取、测试、编辑、校验和格式查询，支持 `--json` 与固定的退出码，详见[命令行说明](apps/cli/README.md)。应用包内的 `M7Arc.app/Contents/Helpers/m7arc` 可直接在终端运行，随包还附带官方 `7zz`。

## 安装与版本

[官网与下载](https://hyyyyyyz.github.io/M7Arc/)提供功能介绍、Apple Silicon 和 Intel 安装包入口，以及首次安装说明。

[GitHub Releases](https://github.com/hyyyyyyz/M7Arc/releases) 提供 DMG 安装附件。打开与你的处理器对应的安装包，将 M7Arc 拖入“应用程序”后启动。Qt 和 7-Zip 运行依赖均包含在应用中。

当前可执行预览版本是 **0.1.0-alpha.2**。修复提升 patch，新增功能提升 minor，不兼容变化提升 major；预览构建递增 `alpha.N`、`beta.N` 或 `rc.N`。`VERSION` 是应用、DMG 和 Release 的唯一版本源。详见[版本规则](docs/VERSIONING.zh-CN.md)。

当前发行使用 ad-hoc 签名，没有 Developer ID 签名和 Apple 公证。如果 macOS 阻止首次启动，可按[Apple 的说明](https://support.apple.com/en-us/102445)在“系统设置 → 隐私与安全性”中对已确认来源的应用选择“仍要打开”。

## 从源码构建

完整源码保存在此项目内，构建不依赖旁边的上游 checkout。需要 macOS 开发工具、CMake、Ninja、Python 和 Qt 6.11.2；构建并打包 `m7arc` 还需要 Rust（cargo），没有 cargo 时配置会给出提示并跳过它。

```sh
cmake --preset release
cmake --build --preset release --parallel 4
ctest --preset release
open build/release/M7Arc.app
```

只构建命令行时不需要 Qt：在仓库根目录运行 `cargo build --release`，得到 `build/cargo/release/m7arc`。Qt 安装、架构选择和打包命令见[构建指南](docs/BUILDING.md)。[发行指南](docs/RELEASE.zh-CN.md)说明 Git 标签、GitHub Actions 和 Release 附件流程；[贡献指南](CONTRIBUTING.md)说明开发约定。

## 项目布局

```text
M7Arc/
├── CMakeLists.txt / CMakePresets.json   # 顶层入口
├── Cargo.toml                            # Rust 工作区
├── VERSION / CHANGELOG.md
├── cmake/                    # 引擎源清单与 sevenzip_objects / m7arc_engine
├── src/
│   ├── engine/               # 7-Zip 适配、格式目录与回调；capi/ 是 C 接口
│   └── platform/             # MacMetadata：元数据采集与恢复
├── crates/
│   ├── m7arc-sys/            # C 接口的 Rust 绑定
│   └── m7arc-core/           # 安全的 Rust API
├── apps/
│   ├── cli/                  # m7arc 命令行（Rust）
│   └── desktop/              # Qt 桌面端
│       ├── src/              # app/ 应用层、ui/ 界面、platform/ 系统集成、tools/ SFX 构建、extractor/ 提取器
│       ├── resources/        # 图标、翻译、格式目录、离线帮助、Quick Actions
│       ├── cmake/            # 官方 7zz 与系统扩展的构建配置、Info.plist
│       └── tests/            # 应用、平台与工具集成验证
├── tests/engine/             # 引擎与 C 接口测试
├── packaging/macos/          # 部署、ad-hoc 签名、DMG、Release
├── resources/brand/          # 品牌资产
├── docs/                     # 设计、接口、构建与发行文档
├── design/                   # 离线交互原型
├── website/                  # GitHub Pages 官网与下载入口
└── third_party/              # 7zip/ 官方完整源码、patches/ 本地补丁
```

## 设计资料

[品牌与图标](docs/BRAND.zh-CN.md)记录「折页 7」标志、字标、配色和使用方式，可编辑 SVG 保存在 `resources/brand/`。

[总设计](DESIGN.zh-CN.md)、[功能迁移矩阵](docs/FEATURE_PARITY.zh-CN.md)、[格式能力](docs/FORMAT_CAPABILITIES.zh-CN.md)、[窗口和参数](docs/DIALOG_SPEC.zh-CN.md)、[文件管理工作流](docs/FILE_MANAGER_SPEC.zh-CN.md)与[阶段范围](docs/ROADMAP.zh-CN.md)定义产品功能。[源码地图](docs/SOURCE_MAP.zh-CN.md)记录上游模块关系；[本地补丁](docs/UPSTREAM_PATCHES.zh-CN.md)记录随项目维护的上游改动。

[交互原型](design/index.html)可直接离线打开；其示例文件和性能数据用于设计演示。原型源码在 `design/prototype/`，执行 `python3 design/prototype/build.py` 可重新打包。

## 已知限制

预览版本尚未完成 macOS 13、Intel 真机和全部归档样本的发行验收。Apple Silicon 上通过 Rosetta 执行 Intel 测试不能替代 Intel 真机验证；Windows SFX 的载荷与结构验证也不能替代 Windows 实际运行。具体环境和结果见[发行验证记录](docs/VALIDATION.zh-CN.md)。

当前内建引擎支持 RAR/RAR5 解压，不支持 RAR 创建；Zstandard 为解码能力；WIM 创建使用 Store。镜像浏览不提供磁盘挂载、修复或镜像写入。硬链接记录仅在支持它的 TAR/WIM 容器中保存。

一个文件管理窗口同时运行一个引擎作业。系统扩展的显示与 macOS 注册和用户启用状态有关。其他解压器是否恢复 7z/TAR 中的 AppleDouble 元数据取决于其实现。

## 许可证

原创应用代码采用 [MIT](LICENSE)。7-Zip 原有部分继续遵循 [LGPL、BSD、公有领域声明与 unRAR 条款](third_party/7zip/DOC/License.txt)，迁移的上游代码保留原声明。Qt 采用动态链接，发行包包含对应模块的许可、源码获取方式与库替换说明。不能将整个安装包描述为仅使用 MIT 许可证。
