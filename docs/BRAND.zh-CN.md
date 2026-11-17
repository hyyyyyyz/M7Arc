# 品牌与图标

M7Arc 的标志是「折页 7」：一条暖橙色带折成数字 7，横向圆头与斜向笔画形成连续轮廓，右上折痕呼应文件的折叠与收纳。应用图标使用米白圆角底座；项目字标使用透明背景，适配浅色和深色页面。

M7Arc 目前是工作名：M 代表 Mac，7 代表 7-Zip，Arc 来自 archive。图形方向已经确定，正式产品名称尚待确定。

## 资产

项目图形标志和字标以可编辑 SVG 保存，客户端图标提供透明边缘的 1024 px PNG 母版。资产位于 [resources/brand](../resources/brand/)。

| 文件 | 用途 |
| --- | --- |
| [app-icon.png](../resources/brand/app-icon.png) | 1024 px 客户端图标母版 |
| [mark.svg](../resources/brand/mark.svg) | 彩色图形标志，透明背景 |
| [mark-mono-dark.svg](../resources/brand/mark-mono-dark.svg) | 浅色背景上的炭黑单色标志 |
| [mark-mono-light.svg](../resources/brand/mark-mono-light.svg) | 深色背景上的米白单色标志 |
| [wordmark-light.svg](../resources/brand/wordmark-light.svg) | 浅色页面上的项目字标 |
| [wordmark-dark.svg](../resources/brand/wordmark-dark.svg) | 深色页面上的项目字标 |

字标的 `M7Arc` 保留为可编辑文本，字体序列为 `Arial, Helvetica, sans-serif`。显示字体由使用环境提供；SVG 不嵌入字体文件。需要固定排版的印刷或演示材料，可在设计软件中将文字转为轮廓后导出。

客户端图标及其尺寸由 [make_icon.swift](../packaging/macos/make_icon.swift) 维护。它与图形标志共用「折页 7」的轮廓，增加适合 Dock 的米白圆角底座、柔和阴影和折痕高光。底座外部透明；各档尺寸均从矢量路径直接绘制。

## 导出客户端图标

在 macOS 上从仓库根目录运行：

```sh
swift packaging/macos/make_icon.swift out/brand/M7Arc.iconset
iconutil -c icns out/brand/M7Arc.iconset -o apps/desktop/resources/icons/M7Arc.icns
cp out/brand/M7Arc.iconset/icon_512x512@2x.png resources/brand/app-icon.png
```

图标集包含 16、32、128、256、512 pt 的 1× 和 2× 版本，最大为 1024 px。提交 Swift 绘制源、PNG 母版和 ICNS；`out/brand` 是本地导出目录。正常应用构建将 [M7Arc.icns](../apps/desktop/resources/icons/M7Arc.icns) 放入 `.app`，图形自解压应用沿用同一图标。

## 色彩与使用

| 颜色 | 色值 | 用途 |
| --- | --- | --- |
| 暖橙 | `#FF7018` | 品牌主色 |
| 亮橙 | `#FF8C32` | 折页受光面 |
| 深橙 | `#E64A08` | 折页内侧 |
| 米白 | `#F7F1E6` | 图标底座、深色背景字标 |
| 炭黑 | `#252422` | 浅色背景字标、单色标志 |

图形按正方形等比缩放，周围保留至少图形宽度的八分之一作为留白。透明背景标志适用于文档、网站和宣传材料；应用图标使用独立的圆角底座版本。图形建议不小于 24 px，项目字标建议不小于 168 px 宽。

单色版本用负空间保留折痕；保留该间隙可以让小尺寸图形仍然呈现折页关系。浅色背景使用炭黑版，深色背景使用米白版。

这些原创图形资产采用项目的 [MIT 许可证](../LICENSE)。
