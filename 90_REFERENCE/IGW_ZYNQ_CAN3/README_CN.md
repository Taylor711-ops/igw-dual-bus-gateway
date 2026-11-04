# 设备 3：ZYNQ-7020 单 CAN 接入（待实板验证）

## 本包做什么

主机和现有安路从机的两个逻辑设备保持原有功能；新增一块 ZYNQ-7020 实体板，作为仅接 CAN 的逻辑设备 3：

- 设备 1、2：原从机，CAN + RS485；设备 3：ZYNQ，只接 CAN。
- 设备 3 可通过 CAN 读写易失 16 位测试寄存器，并支持现有 R26 CAN 周期主动上报及网页配置固定值。
- 第一次收到主机对设备 3 的有效读写请求后才开始主动上报，避免单独上电时反复无应答发送。
- 新的主机监控副本默认采样三个设备，但不对设备 3 轮询 RS485；选择设备 3 时页面显示 RS485 未接入、禁用跨总线操作。设备 1、2 保留双总线与交叉验证。
- 不支持设备 3 的 RS485、文件/SD、跨总线交叉及 NPU；NPU 原本只分析设备 1。现有文件网页仍只支持设备 1、2。CAN 数值寄存器随设备断电重置。

**当前仅做了本机源代码和模拟通信测试，尚未执行 ARM 目标交叉编译、Vitis 实板运行或实际三板总线测试。** 启动输出不等同于实际通信通过。现有监控与文件原目录不覆盖。

## 文件

- `zynq/main.c` 与四个 `.h`：放进新的 Vitis 设备 3 应用 `src`。
- `zynq/system_wrapper.xsa`：从用户提供的 `16_can_key_led.zip` 原样提取，内含 7020 硬件平台和 bitstream。
- `host/monitor`：基于用户已安装 R26 排版版本的主机监控源码。
- `host/build_on_ubuntu.sh`：在用户 Ubuntu 使用已有 AArch64 交叉编译器构建主机监控。
- `host/install_on_board.sh`：主机板创建独立的 `r26_can3_demo` 副本。
- `run_local_checks.sh`：Linux/GCC + Node 模拟回归；非必须部署步骤。

已检查用户工程 `can_key_led.xpr` 所选器件为 `xc7z020clg400-2`，工具版本 Vivado 2020.2；请核对实际核心板封装和速度等级。如果不符或工程/硬件版本不同，不使用这个 XSA，先重新导出正确平台。

CAN 参考时钟以所提供 BSP 的 `XPAR_XCANPS_0_CAN_CLK_FREQ_HZ=100000000` 为编译条件；程序设置 BRP 寄存器 9、TS1 寄存器 14、TS2 寄存器 3、SJW 寄存器 3，计算得到 500 kbit/s、80% 采样点。实际运行的时钟和位速率仍需要实板核实。厂商例程代码注释假设 24 MHz，不能把原示例直接接入现有 500 kbit/s 系统。

## 先核对硬件

1. 保存现有三台板子的程序、供电和接线；运行中的监控/文件服务先在它的原终端 Ctrl+C 等待退出。
2. 核对 ZYNQ 底板与已上传 V3.9 原理图匹配，核对真实核心板型号为 `xc7z020clg400-2`。不符合时不要使用本 XSA 下载。
3. CAN 收发器 TJA1050 的供电为 5 V；核实 CAN_R/RXD 的实际电平、核心板对应输入的允许范围及是否有电平转换，确认兼容后才连接 P3。不能以手册上的 3.3 V IO 约束代替测量或电平兼容性确认。
4. 确认 CANH/CANL、可靠参考地及线路拓扑。底板 CAN 的 R2 为 120 Ω，需与原系统终端电阻一并核对；不要盲目增加第三个终端。断电接线；不要自行拆改元件。
5. ZYNQ 设备 3 仅连接 CAN，不连接 RS485；关闭旧 UDP 调试面板。实际接线和波特率确认完成前，不要将示例固件连接到原系统 CAN 总线。

## 第一步：Windows，建立 ZYNQ 应用

以下按 Vitis 2020.2 经典界面描述；若实际版本或界面不同，以截图为准。此阶段不写 QSPI，也不格式化 SD 卡。

1. 保留 `16_can_key_led` 原工程；将本包放在一个纯英文路径下（如 `C:\IGW_ZYNQ_CAN3`）。
2. 打开 Vitis，选择一个新的空 workspace。新建应用工程；选择从硬件规格文件新建平台，选择本包 `zynq/system_wrapper.xsa`，处理器 `ps7_cortexa9_0`、运行环境 `standalone`。如让选择模板，选 `Empty Application`。
3. 把 `zynq/main.c`、`igw_protocol.h`、`igw_rw_protocol.h`、`igw_can_rw_protocol.h`、`uplink_protocol.h` 一起复制到新应用的 `src` 中；不要同时保留另一个 `main.c`。
4. Build 应用。若报 `CAN clock` 编译条件不符、平台缺失或头文件错误，保留完整输出，不要盲目改时序常量。该 XSA 与用户原工程一致，不需要为了这个示例重做 Vivado 设计。
5. 硬件电平和接线确认后，用 JTAG 配置该平台 bitstream 并启动新应用；串口应看到 `CAN_READY unit=3`。这只表示控制器配置完成，只有下一步网页收发通过才表示板间通信有效。

## 第二步：Ubuntu，编译主机页面

将本包解压到 Ubuntu 的英文路径。在终端进入解压后的 `IGW_ZYNQ_CAN3`：

```sh
sha256sum -c SHA256SUMS.txt
bash host/build_on_ubuntu.sh
```

默认交叉编译器前缀：`~/uisrc-lab-anlogic/tools/aarch64-linux/bin/aarch64-linux-gnu-`。如路径不同，可执行：

```sh
CROSS_COMPILE=/实际路径/aarch64-linux-gnu- bash host/build_on_ubuntu.sh
```

生成 `host/deploy` 并显示 `CAN3_HOST_BUILD_COMPLETE` 后，将 `host/deploy` 中所有文件复制到主机 SD 卡 `igw_apps/r26_can3_install/`。本机没有该 AArch64 编译器，因此上述目标构建仍需在用户环境运行。

## 第三步：安路主机板安装独立版本

确保原监控、文件和测试服务已停止。主机 Buildroot 终端：

```sh
/etc/init.d/S95igw_gateway stop
cd /mnt/mmcblk0p1/igw_apps/r26_can3_install
sha256sum -c SHA256SUMS.txt
sh install_on_board.sh
```

安装脚本检查进程、锁和已安装 `r26_bidir_ui` 的清单，创建新目录 `/mnt/mmcblk0p1/igw_apps/r26_can3_demo`，不启动服务。若 STOP，保留输出，不要删除活跃锁或跳过检查。

## 第四步：三板受控测试

1. 确认原从机运行 BIDIR-R26，ZYNQ 与主机 CAN 电气兼容、布线及终端已经确认。先启动原安路从机和 ZYNQ，再启动主机监控。
2. 主机板执行：

```sh
cd /mnt/mmcblk0p1/igw_apps/r26_can3_demo
sh run_secure_gateway.sh
```

输入原有归档口令，等待 `R26 CAN3 DEMO ... READY`；电脑 `192.168.31.100` 打开 `http://192.168.31.222:8080`。服务默认 600 秒；终端保持运行。
3. 查看设备 1、2 的 CAN/RS485 均在线；验证设备 1 的双向交叉。选设备 3，CAN 应在线，RS485 显示“未接入”，跨总线按钮不可用。若设备 3 还没准备好，主机 CAN 轮询仍会发生超时，不代表软件已实测通过。
4. 在设备 3 主机→从机中，CAN，寄存器 16，数值 `12345`，点击发送并独立读回；在从机→主机中，CAN，数值 `54321`，等待“从机真实上报已由主机接收”。如果原值相同则换一个值。写入和上报需要 CAN 总线应答正常。
5. 若设备 3 不在线或 CAN 错误递增，停止测试，保存双方串口日志和 `ip -details -statistics link show can0`；不要将启动 READY 记为通信通过。操作结果不明时不要反复重发写入。
6. 演示后在监控终端 Ctrl+C 等待退出、`FINAL_TX_AUTHORIZATION=0x00000000`。原监控版本恢复：

```sh
cd /mnt/mmcblk0p1/igw_apps/r26_bidir_ui
sh run_secure_gateway.sh
```

两个监控版本与文件网页不应同时运行。保存当次证据后再切换。
