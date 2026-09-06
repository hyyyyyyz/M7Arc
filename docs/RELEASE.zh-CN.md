# macOS 构建与 GitHub Releases

发行物是包含 Qt 与 7-Zip 运行依赖的 `M7Arc.app`，以 DMG 上传 GitHub Releases。首批提供 Apple Silicon 和 Intel 两个架构包，最低部署目标为 macOS 13。应用与引擎全部从本仓库构建，外层原始 `7zip/` 目录不参与发行。应用同时提供[完整官方 7zz 和 Console SFX](CLI_SFX.zh-CN.md)。

当前发行采用 **ad-hoc 代码签名，没有 Developer ID 签名，也没有 Apple 公证**。缺少开发者证书不会阻止构建或上传。Ad-hoc 签名用于满足本地代码完整性及执行要求，不认证发布者身份。Qt 的部署工具提供相关签名选项；本项目在所有资源固定后明确完成包内签名。[Qt macOS 部署](https://doc.qt.io/qt-6/macos-deployment.html)

## 下载、安装和更新

| Mac | Release 附件 |
| --- | --- |
| Apple Silicon（M 系列） | `M7Arc-<版本>-macos-arm64.dmg` |
| Intel | `M7Arc-<版本>-macos-x86_64.dmg` |

打开 DMG，将应用拖入 Applications 快捷方式，推出磁盘映像后从“应用程序”启动。Qt、7-Zip 组件和许可证均随包提供，不需要安装 Homebrew 或开发工具。GitHub 自动提供的 Source code ZIP/TAR 是源码；安装应选择 DMG 附件。

首次启动被 macOS 阻止时，确认下载来自本项目 GitHub Releases 后，进入“系统设置 → 隐私与安全性”，对本次 M7Arc 提示选择“仍要打开”，并按系统提示确认。该操作只为这个应用建立例外；受组织管理的 Mac 可能由管理员限制此入口。[Apple 安装说明](https://support.apple.com/zh-cn/102445)

更新时先退出 M7Arc，再将新版应用拖入“应用程序”替换。预览阶段通过 Releases 手动更新。卸载时退出应用并将其移入废纸篓。

## 本地构建

使用 Xcode 15/macOS 14 SDK 或更新工具链、CMake 3.24+、Ninja 和官方 Qt 6.11.2 macOS SDK。开发依赖为 Qt Base、Qt Tools 及 Linguist 工具需要的 Qt Declarative；应用运行使用 Core、Gui、Widgets、Concurrent。Qt 官方 macOS 包提供 arm64 与 x86_64，应用构建明确选择目标架构与 macOS 13.0 部署目标。[Qt 支持平台与架构](https://doc.qt.io/qt-6/macos.html)

在仓库根目录运行；把 `qt_sdk_prefix` 设置为本机 Qt SDK 中包含 `bin/macdeployqt` 的目录：

```bash
qt_sdk_prefix="/path/to/Qt/6.11.2/macos"
cmake -S . -B build/release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$qt_sdk_prefix" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DBUILD_TESTING=ON
cmake --build build/release --parallel 3
ctest --test-dir build/release --output-on-failure
python3 packaging/macos/package.py --qt-prefix "$qt_sdk_prefix"
```

Intel 原生构建改用 `-DCMAKE_OSX_ARCHITECTURES=x86_64`。产物为 `build/release/M7Arc.app` 和 `build/dist/M7Arc-<版本>-macos-<架构>.dmg`。打包脚本也接受 `--app` 与 `--output`，可用于其他构建目录。

DMG 脚本从已有 `.app` 复制临时发行目录，加入项目、7-Zip、Qt 许可证与安装说明，运行 `macdeployqt` 搬迁 Qt 框架及插件。它检查包内各个 Mach-O 的架构、最低系统版本和运行依赖，阻止把指向开发机 Qt/Homebrew 目录的依赖留在公开包内；随后从内到外执行 ad-hoc 签名，并在去掉开发环境库路径后运行 `--smoke-test`。成功后使用 `hdiutil` 生成压缩 DMG，DMG 内有 Applications 快捷方式。

这些验证用于发现缺少架构、错误部署目标、搬迁失效及签名后不能启动等发行故障；它们不替代归档内容往返测试和图形界面的功能验收。测试覆盖以对应 Release 的记录为准。

## 自动构建与发布

[macos.yml](../.github/workflows/macos.yml) 在普通分支 push、Pull Request 和手动启动时构建两个架构，执行 CTest 与实际包内启动验证，并把 DMG 保存为 Actions artifact。标签构建在两个架构均成功后创建 GitHub Release，将 DMG 作为附件上传。

工作流使用 `macos-15` ARM runner 与 `macos-15-intel` Intel runner，避免 `macos-latest` 自动切换系统造成构建基线漂移。[GitHub runner 镜像](https://github.com/actions/runner-images)

发布顺序：

1. 按 [版本规则](VERSIONING.zh-CN.md) 更新 VERSION，将本次实际变更和已知问题写入 CHANGELOG 对应版本。
2. 提交并推送该阶段代码，等待普通 CI 构建和测试结果。
3. 给同一提交创建 `v<VERSION>` 标签并推送。
4. 标签工作流重新构建两个架构并生成 DMG；`alpha`、`beta`、`rc` 自动标记 Pre-release，稳定版本标记正式 Release。

```bash
release_version=$(cat VERSION)
git tag -a "v$release_version" -m "M7Arc $release_version"
git push origin "v$release_version"
```

工作流只使用仓库提供的 `GITHUB_TOKEN` 上传，发布 job 需要 `contents: write` 权限，不需要 Apple 证书或公证 secret。Git 标签必须与 VERSION 完全一致；脚本从 CHANGELOG 提取这个版本的说明，并补充架构与未公证安装说明。现有公开标签和附件保持不变；修复后的公开包使用新版本号。[GitHub CLI Release](https://cli.github.com/manual/gh_release_create)

## 许可和源码

新增应用代码、官方 7-Zip、Qt 各自保留许可。DMG 与 `.app/Contents/Resources/Licenses` 同时包含许可文本、Qt 第三方组件声明和源码地址。完整应用及 vendored 7-Zip 源码随标签提供；Qt 运行库的对应上游源码地址记录在包内 Qt README。

Qt 采用动态链接，用户可以使用兼容的修改版本替换框架，或以修改后的 Qt SDK 重新构建应用。替换后应重新应用本地 ad-hoc 签名；同一个打包脚本可以为重建结果生成 DMG。

## 已知限制

- 目前没有 Developer ID 身份与 Apple 公证票据，初次启动可能需要在系统设置中为 M7Arc 单独放行。
- 构建目标为 macOS 13，不代表所有旧系统与 Intel 设备已经完成实机验证；实际验证环境写入每次 Release。
- Universal 2 是后续合并发行目标。脚本只在应用和全部依赖都具有两个架构时接受 `universal2`，当前 CI 发布两个独立架构包。
- 可执行预览的功能范围以 CHANGELOG 和对应 Release 为准；完整迁移设计不等于该预览已经实现所有功能。

后续获得 Developer ID 后，在资源与依赖固定后执行发行身份签名，再完成公证及票据装订。现有版本号和下载渠道继续沿用；签名状态随 Release 准确记录。
