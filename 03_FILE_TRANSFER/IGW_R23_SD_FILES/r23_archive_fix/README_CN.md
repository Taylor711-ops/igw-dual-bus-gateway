# R23 主机网页归档修补

症状：程序R23打印 `/tmp/igw_r22_files.*`，启动脚本只接受R23目录，退出显示EVIDENCE_SAVED=0。

原因是R23升级时Web程序的临时目录字符串遗漏更新。此修补使启动脚本同时接受旧目录和新目录，所以已经部署的R23二进制无需重新编译。完整R23源码也已将临时目录改为R23，供以后构建。

只修补主机脚本和对应SHA256清单条目；会校验原部署、备份旧脚本和清单，不修改其他二进制、从机、BOOT、DTB、FPGA、SD文件内容。补存失败证据只是复制到主机SD，不把失败传输改为成功。

## 使用

将本文件夹复制到主机SD的 `igw_apps/r23_archive_fix/`。主机文件服务和CLI均停止后，在主机Linux串口执行：

```sh
cd /mnt/mmcblk0p1/igw_apps/r23_archive_fix
sh save_existing_evidence.sh /tmp/igw_r22_files.dSpmSX
sh apply_archive_fix.sh
```

第一条补存工具将现有任务文件及匹配的启动器service.log、CAN前后统计复制到主机SD新目录，保留原始/tmp文件。结束打印SD_EVIDENCE_RECOVERED并显示本轮任务日志末尾。若主机已重启导致/tmp消失，它会明确失败，不创建空证据冒充恢复成功。

第二条安装工具默认修补 `/mnt/mmcblk0p1/igw_apps/r23_sd_files`，先验证原来的SHA256SUMS.txt，确认启动脚本属于已知R23基线，再备份并更新。若脚本不是已知基线，不自动覆盖；请发错误。重复运行已安装修补会提示ALREADY_INSTALLED。

成功后：

```sh
cd /mnt/mmcblk0p1/igw_apps/r23_sd_files
sh run_file_web.sh
```

网页刷新后不必传大文件，选择CAN设备1，取回已有4KiB文件，等待通过，在主机终端Ctrl+C。预期R23_LAUNCHER_EXIT=0 EVIDENCE_SAVED=1并打印SD_EVIDENCE。

旧二进制仍可能显示/tmp/igw_r22_files.*，修补脚本会正确将其保存到r23_file_web归档中。这不再代表失败。以后从修正版完整R23源码构建的Web程序会显示/tmp/igw_r23_files.*。

本修补不会重新运行已经做过的从机固件或存储测试。
