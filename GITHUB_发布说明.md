# GitHub 发布说明

1. 本目录是公开源码快照；比赛 RAR 是原整理包，两者不完全相同。公开快照保留项目源码、网页、脚本及测试源码；不包含 `PUBLICATION_EXCLUDED.tsv` 所列厂商生成 IP、嵌套压缩包、预编译文件和运行日志/证据。
2. 第三方许可证以各文件的原文为准；项目原创代码的统一许可证需由权利人确认后添加。不要给整个混合来源的代码树直接套用一个许可证。
3. 新建一个 Public GitHub 仓库后，在本目录运行：

   ```sh
   git init
   git add .
   git commit -m "Publish industrial gateway source"
   git branch -M main
   git remote add origin <替换为GitHub仓库的HTTPS地址>
   git push -u origin main
   ```

   如 Git 提示未设置作者身份，请在本仓库配置自己的姓名和 GitHub 邮箱；GitHub 可能要求通过浏览器授权。不要把密码或令牌写进代码或聊天。
4. 确认 GitHub 上能看到 `README.md` 和三个主要代码目录，再把仓库网页地址填入比赛系统的 Github 链接栏。
