# R15 启动后首轮业务回归包

用途：在已冷启动 R15 + DTB FIX1 的主机上，复用 SD 卡已校验的 R13 应用，加入被动 PL CAN 帧采集。
不重新生成 BOOT，不修改 DTB，不修改从机，不覆盖原 R13 目录。
本包尚未在板上运行；首次回归建议 60–90 秒，最多 600 秒。
这不是设备主动写入功能版本，该功能在硬件回归后另行实现。

## 1. Ubuntu 虚拟机编译一个采集程序

解压到 ~/IGW_R15_BRINGUP_VM，执行：

```sh
cd ~/IGW_R15_BRINGUP_VM
bash build_capture.sh
```

默认使用 ~/uisrc-lab-anlogic/tools/aarch64-linux/bin/aarch64-linux-gnu-gcc。
只编译采集 C 程序为静态 AArch64 Linux ELF，不重新编译原业务或 FPGA 工程。
编译成功后，将整个 deploy 文件夹复制回 Windows。

## 2. 放到主机 SD 卡

先在主机串口执行 sync、poweroff，待关机后断电、取卡。
将 deploy 内全部六个文件复制到 SD 卡：

```text
igw_apps/r15_bringup_install/
  igw_can_pl_capture
  check_r15.sh
  run_web_gateway.sh
  run_secure_gateway.sh
  install_on_board.sh
  SHA256SUMS.txt
```

保留现有 BOOT.bin/system.dtb 和 igw_apps/multi_r13 原应用。
重新上电，登录后：

```sh
/etc/init.d/S95igw_gateway stop
cd /mnt/mmcblk0p1/igw_apps/r15_bringup_install &&
sha256sum -c SHA256SUMS.txt &&
sh install_on_board.sh
```

安装程序先校验原 multi_r13 完整清单，再将原五个业务程序、NPU 启动脚本、构建记录复制到独立 r15_bringup 目录；最后生成新校验清单。
若原 multi_r13 目录缺失、清单失败或目标已存在，会停止，保留原目录不变。不要自行删除原目录或跳过校验。

## 3. 首轮 60–90 秒回归

从机保持 R13 正常运行，CAN 和 RS485 保持原接线。关闭旧 PC UDP 面板。

```sh
cd /mnt/mmcblk0p1/igw_apps/r15_bringup &&
sh check_r15.sh &&
sh run_secure_gateway.sh
```

口令在串口提示后输入原加密归档使用的私人密码；不要发到聊天。
新启动脚本精确检查 R15 BOOT、FIX1 DTB、四个 live DT disabled 节点、SD 启动源、RS485/CAN 身份/时钟参数/FIFO 深度和 TX 授权为零。
保留原进程互斥、CAN 设置、NPU 450 HARD IRQ/300 向量自检、四个业务服务清理；新增第五个采集子进程一起停止。

看到 READY 后，PC 浏览器打开 http://192.168.31.222:8080，检查两个逻辑设备的 CAN/RS485 数据，做一次原有 CROSS 跨总线写入读回操作，持续观察 60–90 秒。
按 Ctrl+C 停止。应输出 PL 采集统计、FINAL_TX_AUTHORIZATION=0、NPU_CAPABILITY_AFTER=0x000000BF。
最长 600 秒后采集器正常结束会触发业务整体清理，属于本次有界测试限制。

## 4. 停止后取证

```sh
head -3 /tmp/igw_r15_plcan.jsonl
tail -3 /tmp/igw_r15_plcan.jsonl
cat /tmp/igw_r15_plcan.stats
tail -5 /tmp/igw_web_can.log
tail -5 /tmp/igw_web_rs485.log
ip -details -statistics link show can0
devmem 0x8040000C 32
devmem 0x1f0000020 32
```

请保留从启动到 Ctrl+C 的终端输出。网页仍报告原 R13 业务/API/模型版本，这是复用已有二进制的真实版本，不会伪装为全新模型。
Web 快照仍进入原 AES-GCM 归档目录 igw_vault_r11；PL 原始诊断帧只写易失 /tmp，不自动保存到 SD。重启前先反馈诊断结果。

## 判定边界

- 静态身份检查通过不等于推理或 CAN 帧解析通过。
- PL 收集器只读接收 FIFO，唯一 MMIO 写入为 POP=1，不发送 CAN/RS485 报文；实际发送仍由原网关应用完成。
- INITIAL_FIFO、INITIAL_GOOD、INITIAL_DROP 是启动前积累值；CAPTURED 可能包含初始积压。判断丢包需看 DROP_DELTA，并结合剩余队列及序号。
- 本轮先验证有效帧、双总线回归、NPU 自检和正常停止；逐帧与 PS 对照、精确转发延迟和多设备物理验证尚未完成。
- NPU 自检和通信故障诊断不等于设备未来故障预测或分类准确率验收。
- 本轮从机仍为一块板模拟两个独立逻辑设备。
