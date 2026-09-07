# M7Arc 官网

静态介绍与下载页面。入口为 `index.html`，布局样式为 `styles.css`，动效样式与交互为 `motion.css` 和 `motion.js`，品牌资源位于 `assets/`。页面无需包管理器、构建步骤或后端服务。

图标采用悬浮、光影与鼠标倾斜效果，内容随滚动进入视野。JavaScript 只用于渐进增强，介绍、导航和直接下载在关闭脚本时仍可使用。页面提供动效暂停按钮，并遵循系统的“减少动态效果”偏好；场景离开视野或页面切到后台时暂停动画。触摸设备不启用鼠标跟随效果。

下载按钮直接链接已经公开的 `0.1.0-alpha.2` Release DMG，分别对应 Apple Silicon 和 Intel。更新下载版本时，一并更新页面内的版本号、资产 URL 和实际文件大小；仅使用已上传的附件。

本地预览：

```sh
python3 -m http.server 4173 --bind 127.0.0.1 --directory website
```

## GitHub Pages

公开地址：[https://hyyyyyyz.github.io/M7Arc/](https://hyyyyyyz.github.io/M7Arc/)。

仓库的 Settings → Pages → Source 使用 **GitHub Actions**。发布流程为 `.github/workflows/pages.yml`：`main` 分支上的 `website/` 或此流程文件发生变更时自动发布，也可在 Actions → GitHub Pages 中手动运行。

流程直接上传并部署 `website/`，无需构建步骤。页面使用相对资源路径，适用于 `/M7Arc/` 项目子路径。公开部署只包含此目录。

页面中的文件管理窗口为界面示意，文件名与大小用于展示布局。安装说明以当前 Release 和项目发行指南为准。
