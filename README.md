# M7Arc

M7Arc 是项目工作名，正式产品名称尚待确定。M 代表 Mac，7 代表 7-Zip，Arc 来自 archive。

项目计划基于官方 7-Zip 完整引擎，使用 C++ 与 Qt Widgets 构建 macOS 归档与文件管理器。

主界面沿用 7-Zip File Manager 的六组菜单、七按钮工具栏、地址栏、属性列表与单/双面板，采用 macOS 风格的系统字体、轮廓图标、圆角控件和浅色/深色界面。项目目标覆盖上游格式读取、完整压缩参数、加密与分卷、包内编辑和嵌套回写、文件管理、校验、基准测试、macOS 元数据及 Finder 集成。

当前交付包含详细产品与工程规格、可交互原型和官方引擎源码基线。Qt 应用处于规划阶段。完整 7-Zip 26.03 基线包含 61 个 handler、7 类归档写入格式、25 个 codec、10 个 hasher；Hash 清单生成单独提供。

| 交付项 | 当前状态 |
| --- | --- |
| 产品规格、源码映射、交互原型 | 已完成当前设计版本 |
| 官方 `7zz` 命令行引擎 | 已完成 Apple Silicon 构建与有限样本验证 |
| Qt 应用与直接引擎接入 | 尚未实现 |
| 可安装 `.app` / `.dmg`、GitHub 发布流水线 | 尚未实现 |

原型交互检查不代表实际压缩、解压或 macOS 安装验证。

## 分发方式

首版通过 GitHub Releases 提供 DMG 下载。DMG 内包含应用及其 Qt、7-Zip 运行依赖，用户打开后将应用拖入“应用程序”文件夹。提供 Apple Silicon 和 Intel 的独立安装包，完整依赖通过构建流水线打包。当前采用 ad-hoc 签名，不使用 Developer ID，也不进行 Apple 公证；每个 Release 明确标注这一状态。取得证书后可增加签名公证发行。

发布附件由项目构建流程生成。GitHub 自动提供的 Source code ZIP/TAR 为源码快照；面向用户的 DMG 作为独立 Release asset 上传。版本采用 SemVer，首个可执行预览为 `0.1.0-alpha.1`；修复增加 patch，功能增加 minor，完成既定迁移后发布 `1.0.0`。详见[版本规则](docs/VERSIONING.zh-CN.md)与[发行说明](docs/RELEASE.zh-CN.md)。[GitHub Releases](https://docs.github.com/en/repositories/releasing-projects-on-github/about-releases)、[Qt macOS 部署](https://doc.qt.io/qt-6/macos-deployment.html)、[Apple Developer ID](https://developer.apple.com/developer-id/)

## 原型

直接在浏览器打开 [design/index.html](design/index.html)，即可离线体验文件管理器。默认单面板，可使用 F9 切双面板；七个工具按钮依次为添加、提取、测试、复制、移动、删除、信息。

原型包含完整菜单、示例归档浏览、属性多列、格式与方法搜索、七类直接创建格式及三个 TAR 组合预设、参数联动、提取、文件工具、六页设置、校验与基准测试窗口。文件操作、任务进度和性能数据为内置示例。

原型源码位于 [design/prototype/](design/prototype/)。在项目根目录执行：

```sh
python3 design/prototype/build.py
```

脚本打包生成离线入口 `design/index.html` 和应用内预览片段 [design/m7arc-ui.html](design/m7arc-ui.html)，无需安装前端依赖或运行本地服务器。离线入口内嵌随仓库保存的 Lucide 图标库，图标许可一并保留。

开发时可在 Node.js 可加载 Playwright 的环境中运行 `node design/prototype/verify.cjs`，检查关键交互、格式约束及三档宽度的布局。默认使用 macOS 上的 Google Chrome；可通过 `CHROME_PATH` 指定浏览器。检查结果与界面截图位于 `design/verification/`。

## 设计文档

- [总设计与目录架构](DESIGN.zh-CN.md)
- [File Manager 完整菜单、窗口与 Qt 映射](docs/FILE_MANAGER_SPEC.zh-CN.md)
- [全部格式、编码器与创建能力](docs/FORMAT_CAPABILITIES.zh-CN.md)
- [对话框字段、参数联动与任务交互](docs/DIALOG_SPEC.zh-CN.md)
- [界面布局与交互规格](docs/UI_SPEC.zh-CN.md)
- [源码地图与引擎接口](docs/SOURCE_MAP.zh-CN.md)
- [全功能迁移矩阵](docs/FEATURE_PARITY.zh-CN.md)
- [实施路线](docs/ROADMAP.zh-CN.md)
- [官方引擎验证基线](docs/evidence/README.md)

## 项目布局

```text
M7Arc/
├── README.md
├── AGENTS.md
├── DESIGN.zh-CN.md
├── design/
│   ├── index.html            # 离线交互原型
│   ├── m7arc-ui.html         # 应用内预览片段
│   └── prototype/            # 原型源码与 build.py
├── docs/                     # 功能、界面、迁移设计与引擎基线
└── third_party/
    ├── 7zip.UPSTREAM.md       # 上游来源、固定版本与更新方式
    └── 7zip/                 # 官方完整源码与原许可证
```

本目录是独立项目根目录。`third_party/7zip/` 是随仓库提交的源码快照，不含嵌套 Git 仓库；项目构建和文档引用均使用目录内文件。源码基线为 7-Zip 26.03。

## 引擎与许可证

完整官方命令行目标是 `third_party/7zip/CPP/7zip/Bundles/Alone2`，产物为 `7zz`。Apple Silicon 构建与有限样本往返验证见引擎基线；能力范围与 Qt 接入见格式目录和源码地图。

7-Zip 的许可证见 [License.txt](third_party/7zip/DOC/License.txt)。上游涉及 LGPL、BSD、公有领域声明及 unRAR 限制，原有版权和条款随源码保留。新增应用代码与 Qt 依赖的发行安排见总设计。
