# M7Arc 官网

静态介绍与下载页面。入口为 `index.html`，样式为 `styles.css`，品牌资源位于 `assets/`。页面不需要 JavaScript、包管理器或后端服务。

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
