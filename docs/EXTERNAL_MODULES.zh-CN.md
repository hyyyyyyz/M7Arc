# 原生 Codecs / Formats 模块

M7Arc 保留 7-Zip 26.03 的全部内建格式，同时加载采用当前 7-Zip COM 接口的 macOS 动态库。模块和应用需要相同的 CPU 架构；Windows DLL、旧版 p7zip 的虚析构接口不能直接使用。

## 安装位置

主应用在启动时扫描：

- `M7Arc.app/Contents/PlugIns/7zip`
- `~/Library/Application Support/M7Arc/7zip`

每个根目录以及其中的 `Codecs`、`Formats` 子目录接受 `.dylib` 和 `.so` 文件。模块依赖库可放在相邻位置并通过 `@loader_path` 链接。更改模块集合后重新启动应用。在“格式与方法”的外部模块页查看实际加载路径、版本、能力和加载错误。

Quick Look 扩展和独立提取器使用各自应用包内的模块集合。应用包位置通过引擎动态库的实际映像路径确定，不依赖 Quick Look 宿主进程的位置。

## 构建示例

项目中的 `tests/engine/modules/ExampleModule.cpp` 是可独立编译的模块示例，导出 `M7Xor` 方法和 `M7Module` 单文件格式。前者可以由内建 7z handler 编解码；后者通过 `SetCodecs` 调用宿主内建的 Copy encoder／decoder 读写数据。

在已配置的项目中构建：

```sh
cmake --build build/release --target m7arc_example_module
```

也可仅使用源码和 Apple Clang 编译 Apple Silicon 模块：

```sh
clang++ -std=c++20 -dynamiclib -arch arm64 -mmacosx-version-min=13.0 \
  -fvisibility=hidden -I third_party/7zip/CPP \
  tests/engine/modules/ExampleModule.cpp \
  third_party/7zip/CPP/Common/MyWindows.cpp \
  -o M7Example.dylib
```

Intel 构建使用 `-arch x86_64`。示例展示接口与内外方法互用；XOR 是可逆转换，不提供压缩或保密能力。

## 接口与生命周期

模块通过 `GetModuleProp` 声明接口类型和版本。方法目录使用 `GetNumberOfMethods`、`GetMethodProperty`、`CreateEncoder` 和 `CreateDecoder`；格式目录使用 `GetNumberOfFormats`、`GetHandlerProperty2` 和 `CreateObject`。外部 hashers 沿用 `GetHashers`。需要宿主或其他模块方法时实现 `SetCodecs`，并在收到空指针时释放引用。

引擎对象统一启用 `Z7_EXTERNAL_CODECS` 和 `EXPORT_CODECS`。所有作业共享一次初始化的 `CCodecs` 目录，内建及外部格式、方法、hasher 均由同一宿主提供。退出时先释放模块的 `SetCodecs` 引用，再卸载动态库。

开发调用者可通过 `EngineOptions::additionalModuleDirectories` 增加显式目录，或用 `loadUserModules=false` 限定为随包集合。配置必须在进程第一次构造 `ArchiveEngine` 时确定。能力值对象中的 `modulePath` 为空表示内建来源。

声明写入能力的外部格式可以使用默认参数创建。引擎仅向已知内建压缩格式传递默认等级和线程属性；外部格式的自定义方法与属性由调用者显式指定。因此不实现 `ISetProperties` 的写入 handler 也可正常使用。

## 验证

`engine.external_modules` 加载真实 Mach-O 模块，使用其外部方法创建、测试及提取 7z 数据；自动识别外部格式，并通过宿主 Copy 方法完成默认创建、测试和提取的字节往返。测试同时验证多作业共享、内建目录完整保留、重复链接发现、无效动态库的错误报告，以及官方内存请求的继续／取消回调。
