# 双网页界面整理

此包只调整监控页面与文件页面的 HTML/CSS 排版，保留两页面原有 JavaScript、后台源码、API 协议和交互逻辑。监控页新增功能导航与状态、双向操作、交叉验证、主动上报、NPU、记录分区；文件页按选择目标、发送、取回、结果分区。地址和操作功能不变：监控8080，文件8081，两服务分时运行。

本地模拟前端测试、DOM 标识检查及本地原生编译通过。此包尚未在板端实际交叉编译和浏览器联调，比赛前请先实测。主从机固件、BOOT.bin、DTB、PL bitstream 不需要更改。

## 预览

电脑上可分别打开 monitor/web/index.html、file/file_page.html 查看排版；作为本地文件打开时没有网关后端，按钮状态和数据不代表运行状态。正式使用需完成以下编译、安装。

## Ubuntu 虚拟机：编译两个主机网页服务

在 Ubuntu 主目录解压本包，进入本目录：

```sh
cd ~/IGW_R26_UI_REFRESH
bash build_ui.sh
```

默认使用 `~/uisrc-lab-anlogic/tools/aarch64-linux/bin/aarch64-linux-gnu-gcc`。若不在此路径，在同一命令中设置编译器前缀，例如：

```sh
CROSS_COMPILE=/实际路径/aarch64-linux-gnu- bash build_ui.sh
```

出现 `AARCH64_ELF64=PASS` 和 `UI_BUILD_COMPLETE` 后，将新生成 `deploy` 内全部文件复制到主机 SD 卡：`igw_apps/r26_ui_install/`。不要把源文件覆盖原 R26 或 R23 目录。

## 主机板：安全分目录安装

先按 Ctrl+C 停止正在运行的监控、文件或测试启动器，等待其退出；然后：

```sh
/etc/init.d/S95igw_gateway stop
cd /mnt/mmcblk0p1/igw_apps/r26_ui_install
sha256sum -c SHA256SUMS.txt
sh install_ui_on_board.sh
```

安装器检查当前服务、原有目录清单和此前验收时记录的 R26/R23 网页程序哈希；不符时停止并请保留输出，不要绕过。通过后建立独立副本 `r26_bidir_ui`、`r23_sd_ui`，只替换副本中的相应网页服务程序并更新副本清单。原版 `r26_bidir_demo` 与 `r23_sd_files` 保留。安装不会自动启动服务。

## 板端试运行

监控页面（要求从机是现有 BIDIR-R26）：

```sh
cd /mnt/mmcblk0p1/igw_apps/r26_bidir_ui
sh run_secure_gateway.sh
```

输入现有归档口令，READY 后打开 `http://192.168.31.222:8080`，可用 Ctrl+F5 刷新浏览器。先测试设备选择、主机发送并独立读回，再测试从机上报、交叉验证和 NPU。请使用可改动的测试寄存器；R27–R30为上报配置保留。

Ctrl+C 等服务退出，再测试文件页面：

```sh
cd /mnt/mmcblk0p1/igw_apps/r23_sd_ui
sh run_file_web.sh
```

打开 `http://192.168.31.222:8081` 并刷新；验证设备/总线切换、选择小文件、下发读回校验、另一总线取回和下载。上传会替换选中逻辑设备的文件；先确认不是重要文件。

若任一页面有异常，保留日志和任务 ID；停止该启动器后，可直接回到原有目录 `r26_bidir_demo` 或 `r23_sd_files` 启动原版。原版服务与新版不能同时占用总线或端口。

## 验收口径

双协议交叉、PL采集、主动上报、监控加密归档及文件存储已有测试证据。严格单向以太网转发时延和多个物理从机并发未直接验证，NPU分类准确率仍待验证，不能据此声称所有中级指标都已正式验收。完整往返短测 408 次均小于50ms，但不等同于直接测得单向延迟。
