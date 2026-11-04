# 设备 3：RS485 文件收发扩展

本包为已有 Zynq-7020 设备 3 和主机文件网页增加文件收发。设备 1、2 的现有文件协议继续使用；设备 3 只支持 RS485，文件存放在 **Zynq 板自己**的 TF 卡上。三个设备共用一条 RS485 总线，本版文件服务统一为 9600 bit/s、8E1，一次只执行一个任务。CAN 文件传输仍可用于设备 1、2。

网页支持从电脑下发并独立读回校验、取回设备现有文件、让设备生成测试文件并取回。每个设备只保留一个可取回的当前文件，设备 3 不提供 TF 卡目录浏览；新文件提交会替换设备 3 的当前文件。单文件协议上限 8 MiB。9600 bit/s 很慢：1 MiB 单向传输仅线路发送就超过 20 分钟，下发加读回会更久；8 MiB 可能需要数小时。请从 4096 字节开始。

**当前状态：本地测试通过，尚未在本环境运行 Vivado 综合、Vitis 交叉编译或实板文件传输。** 接下来的两个板端构建和小文件测试是必需的，不应把本地模拟结果当作板上通过。

## 1. 准备 Zynq 板

- 保持之前正常使用的 RS485 A0/B0 连接和 P1 跳帽设置。设备 3 的数字读写、上报 PL 逻辑基于已验证版本；本次在旁边增加 PS 文件服务。
- Zynq 的 TF 卡槽需要插入一张可用的 FAT32 卡，建议至少有 32 MiB 空闲。程序只使用卡内 `IGW3` 目录，创建前检查目录归属；若该目录属于其他内容或无法挂载，会拒绝文件操作。程序不自动格式化卡片。需要取回的现有文件须已由本程序写入该设备的文件槽，不能直接浏览卡上其他目录。
- 新设计需要 Zynq 的 PS 程序持续运行；通过 JTAG 加载是临时运行，掉电后要重新加载。本包不会制作或刷写 BOOT、QSPI、主机 DTB 等。

## 2. Windows：创建 Vivado 工程并仿真

将压缩包解压到 `C:\IGW_ZYNQ_RS4853_FILE`。在 Vivado 2020.2 关闭旧仿真/工程后，Tcl Console 中执行：

```tcl
source {C:/IGW_ZYNQ_RS4853_FILE/zynq/create_project.tcl}
source {C:/IGW_ZYNQ_RS4853_FILE/zynq/run_guard_sim.tcl}
```

确认有 `TB_GUARD=PASS`，再关闭本次仿真并测试原有数字协议：

```tcl
close_sim
source {C:/IGW_ZYNQ_RS4853_FILE/zynq/run_numeric_sim.tcl}
```

确认 `TB_RESULT=PASS: RTU read/write, grant enable/disable, nonce replay, CRC and one-shot RS485 uplink`。关闭仿真后构建：

```tcl
close_sim
source {C:/IGW_ZYNQ_RS4853_FILE/zynq/run_build.tcl}
```

确认实现完成、setup/hold 时序余量非负，检查 DRC，并确认生成 `C:/IGW_ZYNQ_RS4853_FILE/zynq/igw3_files.xsa`。如工程创建、仿真、时序或 DRC 失败，保留完整报错，先不要下载板子。

## 3. Windows：Vitis 构建并启动设备 3

1. 在 Vitis 2020.2 新建应用和平台，硬件选择上面生成的 `igw3_files.xsa`，处理器选择 `ps7_cortexa9_0`，操作系统选择 `standalone`，应用模板选 Empty Application。
2. 打开平台的 Board Support Package Settings，启用 `xilffs`，保持可读写；本工程使用短文件名，不需要开启长文件名。重新生成/构建平台。
3. 将 `zynq/src` 中全部文件导入应用 `src`：`main.c`、`file_engine.c`、`file_engine.h`、`igw_protocol.h`、`igwf_sha256.h`、`rsf_wire.h`、`sd_store.inc`、`rsf_sd_protocol.inc`。构建应用。不要把旧 CAN3 的 `main.c` 混入本应用。
4. 使用 JTAG 加载本 XSA 对应的 bitstream 并在 `ps7_cortexa9_0` 运行新应用；可以使用 Vitis 的 Launch on Hardware。此步骤会临时替换设备 3 当前 PL 内容，不写非易失启动存储。保持 JTAG 和程序运行。
5. 连接 Zynq 原来的 PS 调试串口，115200、8N1。成功时应看到 `IGW3_FILE_SD=READY` 和 `IGW3_FILE_UART=READY 9600 8E1 unit=3`。若 SD 显示 FAIL，记录 reason 和 fresult；确认卡片格式及插入后重新启动应用。不要格式化有数据的卡。

数字读写和主动上报在新硬件组合下也要重新确认；原有 PL 的仿真通过不能代替新组合实测。若要恢复旧方案，停止文件服务，在 Vivado 重新用上一版 `IGW_ZYNQ_RS4853_UPLINK` 的 bitstream 进行 JTAG 下载；主机也切回原文件服务目录。

## 4. Ubuntu：交叉编译主机文件服务

将压缩包放在 Ubuntu 主目录，执行：

```sh
igw3f_work=$(mktemp -d "$HOME/igw3file.XXXXXX")
unzip -q "$HOME/IGW_ZYNQ_RS4853_FILE.zip" -d "$igw3f_work"
cd "$igw3f_work/IGW_ZYNQ_RS4853_FILE"
bash host/build_on_ubuntu.sh
```

默认使用 `~/uisrc-lab-anlogic/tools/aarch64-linux/bin/aarch64-linux-gnu-gcc`。若工具链在其他位置，执行前设置 `IGW_AARCH64_CC` 为实际编译器完整路径。看到 `AARCH64_ELF64=PASS` 后，把 `host/deploy` 里面的所有文件复制到 **主机板 SD 卡** 的 `igw_apps/r23_sd_unit3_install/`。

## 5. 主机板：安装并启动文件页面

先在对应终端按 Ctrl+C 停止手动启动的监控/文件服务并等待其退出，然后在主机板串口执行：

```sh
/etc/init.d/S95igw_gateway stop
cd /mnt/mmcblk0p1/igw_apps/r23_sd_unit3_install
sha256sum -c SHA256SUMS.txt
sh install_on_board.sh
```

安装器检查原 `r23_sd_ui` 的校验和和总线占用，然后建立独立目录 `r23_sd_unit3`；不会覆盖原目录，也不会自动启动服务。若报告锁或进程占用，不要删除锁，先确认对应服务已退出。

启动：

```sh
cd /mnt/mmcblk0p1/igw_apps/r23_sd_unit3
sh run_file_web.sh
```

电脑访问 `http://192.168.31.222:8081`，Ctrl+F5 刷新。页面服务与监控页面分时使用；如需监控，先 Ctrl+C 退出文件服务，再启动之前的监控服务。

## 6. 首次板上验证

1. 在监控模式确认设备 3 的 RS485 数字读写和上报仍正常，然后停止监控。
2. 启动文件页面，选择“设备 3 · Zynq”，应自动选择 RS485。选一个可覆盖的 4096 字节测试文件，点击“下发并读回校验”，等待 `校验通过`。
3. 点击“取回设备现有文件”，等待完成并下载；比较下载文件与原文件的长度和 SHA-256。还可以用“设备生成并取回”测试设备侧生成。每次测试会替换该设备当前文件。
4. 停止文件服务，重启 Zynq 应用后再次取回文件，可验证一次重启后仍可访问。之后再测试设备 1、2 的原有文件收发。不要把一次重启测试当作完整掉电恢复验证。

如果任何一步失败，保留网页任务 ID、主机文件服务日志及 Zynq 串口输出；不要连续重发写命令。失败/取消后的文件页面需按提示检查并重启文件服务。

## 验证范围

本地执行了设备 3 的 FAT 模拟盘写入、读取、重新挂载、无主目录拒绝、CRC/设备隔离测试；模拟 RS485 协议下完成 1 MiB 文件下发及读回；网页实际 C 处理程序测试设备 3、总线限制与单任务保护；设备 1/2 的原协议模拟跨总线读写；安装器在临时目录测试保留原版。设备软件源文件还用用户提供的 Vivado/Vitis 2020.2 BSP 头文件做了本机语法检查。这些都不是 AArch64/ARM 实际链接、Vivado 仿真/综合、真实 TF 卡读写或板间通信测试。
