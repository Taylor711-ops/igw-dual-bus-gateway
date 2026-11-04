# CAN 设备 1 与 RS485 设备 3：文件并行测试版

本版本在现有 `r23_sd_unit3` 文件服务基础上，只允许 **CAN／设备 1** 和 **RS485／设备 3（Zynq）** 两个文件任务并行。其他合法目标仍可单独使用；其他两个任务的组合会被拒绝，不排队。同一时刻最多两个任务。网页依旧独立，运行文件模式前须停止监控服务。

**本次不改设备端程序**：沿用此前已测试的设备 1 SD 文件程序和 Zynq 设备 3 的 PL/PS 文件程序，设备 3 的 TF 卡保持插入。沿用 CAN 500 kbit/s、RS485 9600 bit/s、8E1。服务仍限定现有主机地址 `192.168.31.222:8081` 和电脑地址 `192.168.31.100`；本次不扩展为任意电脑同时访问。

开发机已做本地模拟：两台独立虚拟从机，使用实际 CAN/RS485 文件协议代码，在任务时间重叠时向两台设备分别下发、读回并核对文件；还测试了非法并行组合、异常和取消。**现场两条物理链路的并发、AArch64 交叉构建、掉电恢复仍需实际验证。**

## 1. Ubuntu 交叉编译

把 `IGW_CAN1_RS3_PARALLEL.zip` 放在 Ubuntu 用户主目录，打开 Ubuntu 终端执行：

```bash
igw_parallel_work=$(mktemp -d "$HOME/igw_parallel.XXXXXX")
unzip -q "$HOME/IGW_CAN1_RS3_PARALLEL.zip" -d "$igw_parallel_work"
cd "$igw_parallel_work/IGW_CAN1_RS3_PARALLEL"
bash host/build_on_ubuntu.sh
```

使用原来的 AArch64 编译器 `~/uisrc-lab-anlogic/tools/aarch64-linux/bin/aarch64-linux-gnu-gcc`；若不在此位置，可设置 `IGW_AARCH64_CC` 为完整路径。出现 `AARCH64_ELF64=PASS` 和 `PARALLEL_HOST_BUILD_COMPLETE` 才进入下一步。压缩包校验失败或编译报错时，保留完整终端输出。

将 `host/deploy` **里面的全部文件**复制到主机 SD 卡：

```text
igw_apps/r23_can1_rs3_parallel_install/
```

不要把 `deploy` 文件夹本身套一层。该目录应直接包含 `igw_file_web`、`igw_can_block`、`igw_rs485_file`、`install_on_board.sh` 和 `SHA256SUMS.txt`。

## 2. 主机安装和启动

退出当前前台运行的文件服务（该终端按 Ctrl+C，等待 `R23_LAUNCHER_EXIT`）；如果监控服务在运行，在主机串口执行 `/etc/init.d/S95igw_gateway stop`。等待服务真正退出，再执行：

```sh
cd /mnt/mmcblk0p1/igw_apps/r23_can1_rs3_parallel_install
sha256sum -c SHA256SUMS.txt
sh install_on_board.sh
```

出现 `PARALLEL_FILE_STAGED=/mnt/mmcblk0p1/igw_apps/r23_can1_rs3_parallel` 表示完成安装。安装器核验已有的 `r23_sd_unit3`，建立新目录，不覆盖原目录，也不自动启动。若显示锁或相关进程还在运行，请先核实相关服务已退出；不要手动删除正在使用的锁。

启动新文件服务：

```sh
cd /mnt/mmcblk0p1/igw_apps/r23_can1_rs3_parallel
sh run_file_web.sh
```

等待 `R23 CAN1 RS3 PARALLEL READY`；该串口窗口要保持运行。网页地址是 `http://192.168.31.222:8081/`。如果没有 READY，把串口完整输出保存下来。

## 3. 先并行取回，再测试并行下发

1. 确认两块从机都在运行，CAN 和 RS485 按现有已验证方式连接。设备 3 串口应有 `IGW3_FILE_SD=READY` 和 `IGW3_FILE_UART=READY 9600 8E1 unit=3`；这块 Zynq 此前用 JTAG 下载，掉电重启后需要重新加载之前测试通过的程序。
2. 两台设备各自已经有此前文件时，在网页点击 **并行取回设备 1 和 3 的现有文件**。此操作不更改设备中的文件。如果设备没有现有文件，先单独创建小测试文件；下发或设备生成会替换该设备的当前文件，重要文件先备份。
3. 结果区应出现 **CAN · 设备 1 · get** 和 **RS485 · 设备 3 · get** 两条任务，可能因速度不同而先后完成。分别达到“校验通过”后，可下载两个文件。两条都是 PASS 只说明均完成，还需按下节查看日志中的重叠时间，才能证明并行。
4. 需要测试并行下发时，先选择 RS485／设备 3，选一个文件并点击“下发并读回校验”；在它仍运行时切换 CAN／设备 1，选择第二个文件并点击同一按钮。两次均会更新相应设备文件，并分别读回做 SHA256 验证。建议先用 4096 字节文件；若切换时任务已经完成，可为设备 3 使用约 16 KiB 的测试文件，以留出操作时间。
5. 只允许 CAN1 + RS4853 并行；其他组合会在网页禁止或返回冲突。设备 1 和 2 同属另一块从机，其 SD 文件槽共享物理硬件，不在本次并行范围。RS485 上多个从机共用一条半双工总线，不会同时驱动发送；本版文件服务不提供多个 RS485 任务排队。

按钮“停止全部传输”会停止当前全部任务。任一失败后不自动重发：保留当前日志、确认另一个正在运行的任务结束，并检查总线和从机。服务退出会保存证据。为防止与其他进程冲突，异常退出或停止后锁可能保留；排查前不要随意删除锁或重启多个文件服务。

## 4. 判定并行和导出证据

测试完成后在启动文件服务的主机串口按 Ctrl+C，等待看到 `SD_EVIDENCE=...` 和 `R23_LAUNCHER_EXIT=...`。然后将刚才输出的 `SD_EVIDENCE` 目录中的 `service.log`、任务 `.log` 和网页结果截图保留。

`service.log` 对每个底层传输阶段记录 `FILE_BACKEND_BEGIN` 和 `FILE_BACKEND_END`，带 `bus`、`unit`、`phase` 和 `mono_ms`。对并行取回的两条 `get`，分别找到 CAN unit=1 与 RS485 unit=3 的开始、结束时间；若两段区间重叠、两任务均校验通过，可记录为两个传输程序在实板上的运行区间重叠且均成功。这个时间区间包含进程开销，不能精确证明两个总线的帧在同一瞬间都处于发送状态；需要验证物理波形重叠时，应同步采集两条线路。各任务 SHA256 不必相同，它们可能是不同文件。

查看最近一次保存的服务日志（如果有多次会话，以串口实际显示的 `SD_EVIDENCE` 路径为准）：

```sh
igw_evidence=$(ls -td /mnt/mmcblk0p1/igw_evidence/r23_file_web/igw_r23_files.* | head -n 1)
cat "$igw_evidence/service.log"
```

## 5. 回退

停止并行版服务并确认它已退出、总线已释放；回到原文件服务目录启动：

```sh
cd /mnt/mmcblk0p1/igw_apps/r23_sd_unit3
sh run_file_web.sh
```

旧目录和设备 3 程序不被本安装器覆盖。若异常导致锁保留，先依据日志确认没有任何旧进程或从机还占用总线；不要在原因不明时强行清理。恢复监控按原有监控服务操作。
