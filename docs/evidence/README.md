# 官方引擎基线

日期：2026-09-06。上游为 7-Zip 26.03，提交 `0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17`。

- [engine-baseline.json](engine-baseline.json)：构建环境、输入样本定义和测量结果。
- [7zz-info.txt](7zz-info.txt)：该次完整 arm64 构建的格式、codec 与 hasher 注册目录。

构建目标对应 `third_party/7zip/CPP/7zip/Bundles/Alone2`，调用 `make -j4 -f ../../cmpl_mac_arm64.mak O=<临时输出目录>`，原源码未修改。产物为官方全格式 `7zz`，运行输出标明 arm64 与 ASM。

样本包括一个 UTF-8 中文名称文本文件、一个模式为 0755 的普通脚本，以及一个空目录。文本文件额外设置 `com.apple.metadata:m7arc-probe=m7arc-test`。所有样本均为本次验证自行创建的内容。

对 7z、ZIP、TAR 分别执行 `a -t<format>`、`t`、`x`，比较普通文件内容的字节序列，并读取输出名称、空目录、可执行位与扩展属性。三种格式的文件内容、名称、空目录、可执行位均保留；扩展属性未保留。

另外对同一组输入执行 7z 文件名加密（`-mhe=on`）和 128 字节分卷（`-v128b`），测试并解压结果，再对中文文件进行直接字节比较。两项均成功，分卷样本生成 3 个部分。

这些结果用于确定“官方引擎可直接复用”和“macOS 元数据仍需实现”的开发边界。运行环境为 macOS 27.0/Apple clang 21/arm64；Qt 库化、所有格式互操作、Intel 与最低支持系统仍按实施路线验证。
