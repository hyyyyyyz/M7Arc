# 官方命令行与自解压包

应用包含从同一份 7-Zip 26.03 源码构建的两个工具：

| 应用包路径 | 用途 |
| --- | --- |
| `Contents/MacOS/7zz` | 完整官方命令行，包括归档读写、格式信息、摘要与基准测试 |
| `Contents/MacOS/7zCon.sfx` | 当前 Mac 架构的命令行自解压器模块 |
| `Contents/MacOS/M7ArcExtractor` | 当前 Mac 架构的独立 Qt 图形提取器 |
| `Contents/Resources/Windows SFX/7z.sfx` | 官方 Windows x86 图形自解压模块 |
| `Contents/Resources/Windows SFX/7zCon.sfx` | 官方 Windows x86 命令行自解压模块 |
| `Contents/Resources/Windows SFX/7zSD.sfx` | 官方 Windows x86 安装器模块 |

GUI 的归档浏览和操作直接接入引擎。随包 CLI 同时为熟悉 7-Zip 命令的用户提供完整命令入口：

```bash
"/Applications/M7Arc.app/Contents/MacOS/7zz" i
"/Applications/M7Arc.app/Contents/MacOS/7zz" a archive.7z Documents
"/Applications/M7Arc.app/Contents/MacOS/7zz" x archive.7z -oextracted
"/Applications/M7Arc.app/Contents/MacOS/7zz" h -scrcSHA256 archive.7z
"/Applications/M7Arc.app/Contents/MacOS/7zz" b
```

`i` 列出 61 个格式注册项、25 个 codec 与 10 个 hasher；写入格式和只读格式沿用上游能力。外部 RAR 编码器不包含在工具内。

## 构建接线

根 CMake 设置架构与最低部署目标之后引入 `apps/desktop/cmake/cli.cmake`，定义应用 target 后调用 `m7arc_attach_tools(M7Arc)`。该调用为应用加入工具依赖，复制两个工具到 app，并编译 Qt C++ 的 `tools/SfxBuilder.cpp` 与 `tools/DistributionBuilder.cpp`。

- `m7arc_cli`：构建官方 `Bundles/Alone2`。
- `m7arc_sfx`：构建官方 `Bundles/SFXCon`。
- `m7arc_bundle_tools`：把最新工具复制进应用。
- `tools.integration`：验证完整注册目录和自解压器的实际提取。

构建使用官方 `cmpl_mac_arm64.mak` / `cmpl_mac_x64.mak` 与各 bundle 的 make 规则，明确传入架构及 `-mmacosx-version-min=13.0`。CMake 根据源码和头文件依赖决定是否构建；触发时对对应 bundle 全量编译，使回调接口和对象文件保持一致。对象文件保存在 `build/.../tools/objects/`，vendor 目录保持源码状态。路径中有空格时，通过临时目录别名为原 make 规则提供输出位置。

## 创建 macOS Console SFX

自解压器使用 **7z** payload，适合 LZMA2、LZMA、PPMd 或 Copy 编码，支持对应过滤器与 AES。默认创建参数使用 LZMA2；SFXCon 不包含 BZip2 和 Deflate 解码器。

开发者可用构建出的 `7zz` 制作 payload，再通过应用的自解压对话框，或测试驱动 `m7arc_distribution_builder_tests` 调用同一个 `createConsoleSfx`：

```bash
"build/release/tools/7zz" a -t7z -m0=LZMA2 payload.7z Documents
build/release/m7arc_distribution_builder_tests console \
  build/release/tools/7zCon.sfx payload.7z Documents.command
./Documents.command t
./Documents.command x -oextracted
```

Qt 应用使用 `m7arc::tools::createConsoleSfx(stub, archive, output)`，返回空字符串表示成功，否则返回错误信息。大归档应在操作 worker 中创建；输出路径必须是尚不存在的新文件。应用不需要 Python 运行环境。

创建流程将 7z 追加到当前架构的 Mach-O stub，将 payload 纳入 `__LINKEDIT` 段范围，再为完整文件执行 ad-hoc 签名。产物支持 `t` 测试、`x` 提取、`-o` 输出目录、`-p` 密码和 `-y` 应答。生成后的文件不能再追加内容，否则应重新创建并签名。

`.command` 可由 macOS 的 Terminal 打开。它是 Mac 命令行自解压器，运行时不需要 M7Arc 或 Qt；不会自动执行归档中的文件。与 M7Arc 一样，产物没有 Developer ID 签名或 Apple 公证。

## 已知限制

- Console SFX 对应一个 macOS 架构；分发给另一架构时需要使用相应架构 stub 重新创建。
- Windows `.exe`、macOS `.command` 和图形 `.app` 分别使用对应平台的提取器。
- SFXCon 的解码集合比完整 7zz 小。已有归档使用不支持的方法时，应以 LZMA2 重新创建 payload。
- 自解压器保留上游常规文件内容、目录和权限语义；macOS 扩展属性和资源叉仍需对应归档工作流处理。

## macOS 图形自解压与 DMG

工具窗口可以从相同的 7z 创建设置生成独立图形 `.app`。它包含 Qt、7-Zip 引擎、许可证和归档载荷，接收者无需安装 M7Arc。打开后选择目标文件夹和可选密码，再执行解压；进度、暂停、取消、覆盖询问和密码重试复用主应用的操作控制器。提取器不会自动执行归档中的文件。

支持三种组织方式：

| 输出 | 载荷位置 | 签名 |
| --- | --- | --- |
| 图形 `.app` | `Contents/Resources/Payload.7z` | 载荷固定后对完整 app 作 ad-hoc 签名 |
| 内嵌 DMG | DMG 内的 `.app` 自带载荷 | 与图形 app 相同 |
| 外置载荷 DMG | `.app` 与 `Payload.7z` 位于 DMG 同一目录 | 提取器 app 不因载荷变化而修改 |

图形提取器使用完整内建引擎；默认 LZMA2，支持加密头和中文文件名。外部载荷必须与应用放在同一目录；应用优先使用内嵌载荷。输出应用使用当前 M7Arc 安装包的架构，最低 macOS 13，支持的格式方法不会依赖用户电脑的额外模块。

创建图形包使用已部署 M7Arc 的 `Contents/Frameworks` 与 Qt plugins，因此须从安装包运行 M7Arc，或先按发布流程部署开发构建。构建器将依赖复制到新 app，保持框架签名，再对外层 app 签名并执行严格校验。输出路径必须不存在，取消或失败会清理本次新建的输出。

控制台 SFX 接口位于 `apps/desktop/src/tools/SfxBuilder.h`，图形包、DMG 与 Windows 接口位于 `apps/desktop/src/tools/DistributionBuilder.h`（`SfxBuilder.h` 会一并包含）：

```cpp
SfxBundleOptions options;
options.embedPayload = true;
auto error = createGraphicalSfx(installedApp, payload7z, outputApp, options);
// 内嵌 app 的 DMG：
error = createSfxDmg(outputApp, {}, outputDmg);
// 外置载荷：先令 embedPayload=false，再传入 payload7z。
```

独立提取器也可执行自动化提取，默认跳过已有文件：

```sh
"Example.app/Contents/MacOS/M7ArcExtractor" --extract-to "/path/to/output"
"Example.app/Contents/MacOS/M7ArcExtractor" --extract-to "/path/to/output" --password-file "/path/to/password.txt"
```

## Windows 图形与命令行 SFX

Windows 产物拼接官方 26.03 `7z.sfx` 或 `7zCon.sfx` 与 7z 载荷，输出 `.exe`。两种模块实际为 **PE32 / Intel 80386**，来自官方 x64 安装程序；模块自身架构决定目标，不能按安装程序文件名推断。[官方来源与许可](../apps/desktop/src/tools/windows/UPSTREAM.md)

图形模块提供 Windows 提取窗口；命令行模块面向终端。默认载荷使用 LZMA2，也可使用 LZMA、PPMd、Copy、对应过滤器和 AES。Windows 输出不经过 macOS `codesign`，不会成为可在 Mac 上运行的程序。

本机验证覆盖 PE 结构、原始 stub 字节、7z 载荷位置、加密归档以及通过 7-Zip 的真实提取。尚无 Windows 系统运行验收。

## Windows 安装器配置

“Windows 安装器”是独立输出目标，使用官方 LZMA SDK 26.03 的 `7zSD.sfx`。SDK 原文许可允许分发该 public domain 模块；随应用保存原始 [SDK 说明](../apps/desktop/src/tools/windows/LZMA-SDK.txt) 和[安装器配置文档](../apps/desktop/src/tools/windows/installer.txt)。

安装器在 Windows 上运行时，解压到临时目录，执行配置的程序，等待结束后清理临时文件。模块支持 LZMA、LZMA2、Copy 及对应过滤器，不包含 AES 和 PPMd，因此此目标禁用密码、文件名加密和 PPMd。

| 配置字段 | 含义 |
| --- | --- |
| Title / BeginPrompt | 安装器标题与开始前询问文本 |
| Progress | 是否显示提取进度 |
| RunProgram | 程序命令行，默认 `setup.exe`；`%%T` 代表临时目录 |
| Directory | RunProgram 的程序路径前缀，默认 `.\`；不是解压目的地 |
| ExecuteFile / ExecuteParameters | 用 Windows 文件关联或命令打开指定文件，另传启动参数 |

启动方式在 RunProgram 与 ExecuteFile 中选择一个。构建器把结构化字段编码成 UTF-8 配置，拼接顺序为 `7zSD.sfx + 配置 + Payload.7z`。引号、反斜杠、换行和制表符遵循官方解析规则。M7Arc 创建安装器及本机验证时均不执行配置的命令。

接口为 `createWindowsInstallerSfx(stub, archive, output, WindowsInstallerOptions, cancelled)`。本机测试使用上游 `GetTextConfig` 对输出配置进行实际解析，并从生成的 `.exe` 提取载荷；Windows 进程启动和清理仍需在 Windows 系统验收。

## 分发验证

`tools.windows_sfx` 在 CTest 中验证两种 Windows 输出。`tools.mac_sfx_bundle` 是图形分发测试，配置时传入 `-DM7ARC_SFX_SOURCE_APP=<已部署的 M7Arc.app>` 才会运行，否则 CTest 显示为 Skipped。图形分发测试以已部署 app 为输入，同时生成内嵌和外置 `.app` / DMG，验证独立启动、严格签名、加密提取，并挂载生成的 DMG 后再次执行提取：

```sh
python3 apps/desktop/tests/tools/DistributionIntegration.py \
  --builder build/release/m7arc_distribution_builder_tests \
  --cli build/release/tools/7zz \
  --windows-stubs apps/desktop/src/tools/windows \
  --source-app /Applications/M7Arc.app
```

图形 app 和 DMG 当前均无 Developer ID 签名和 Apple 公证，使用 ad-hoc 签名。接收者的系统阻止打开时，通过系统设置中的“隐私与安全性 → 仍要打开”处理该应用。
