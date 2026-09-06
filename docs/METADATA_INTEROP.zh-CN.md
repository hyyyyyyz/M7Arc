# ZIP 与 macOS ditto 的元数据互操作

M7Arc 与系统 `/usr/bin/ditto` 双向交换 ZIP 时，保留文件内容、执行权限、资源叉、扩展属性、Finder 信息、Finder 标签和 ACL。验证环境为 macOS 27.0（26A5425a）、Apple Silicon、7-Zip 26.03。

| 检查项 | ditto 创建 → M7Arc 提取 | M7Arc 创建 → ditto 提取 |
| --- | --- | --- |
| 文件内容字节 | 一致 | 一致 |
| POSIX 权限 `0751` | 一致 | 一致 |
| 自定义二进制扩展属性 | 一致 | 一致 |
| `com.apple.ResourceFork` | 一致 | 一致 |
| `com.apple.FinderInfo` | 一致 | 一致 |
| Finder 标签二进制 plist | 一致 | 一致 |
| 用户扩展 ACL | 一致 | 一致 |

[自动测试](../tests/platform/MetadataInterop.cpp)创建可执行文件，附加上述真实属性，然后分别调用系统 ditto 与 M7Arc C++ API 完成创建和提取。Finder 标签由系统 `plutil` 编码为二进制 plist，ACL 使用当前用户的显式访问规则。属性按原始字节、权限位和 ACL 文本直接比较。

ditto 创建使用 `-c -k --sequesterRsrc --keepParent --rsrc --extattr --acl`；提取使用 `-x -k --rsrc --extattr --acl`。M7Arc 创建启用 `preserveMacMetadata`，提取启用 `restoreMacMetadata`。

正文成员提供 POSIX 权限；AppleDouble 侧车恢复扩展属性、资源叉及 ACL。侧车自身的文件权限不会替代正文成员的执行权限。

运行：

```sh
cmake --build --preset release --target m7arc_metadata_interop_tests
ctest --test-dir build/release -R '^platform.metadata_interop$' --output-on-failure
```

本记录覆盖 ZIP 与 ditto 的互操作。其他格式的往返能力由对应引擎测试验证；Windows 安全描述符与本机 ACL 分别处理。
