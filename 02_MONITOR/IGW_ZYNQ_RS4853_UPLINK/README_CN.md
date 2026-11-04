# 设备 3：RS485 授权时隙上报

本包在已能读写的 Zynq-7020 设备 3 上增加“从机 → 主机”演示。设备 3 只有一条 RS485，主机为它周期性分配单次发送时隙；设备在时隙内发送其设置值或本地运行秒数，主机收到匹配的完整数据后，网页才显示确认。它不是设备任意时刻抢占总线，也不是设备 1、2 所用的 CAN 申请时隙方式。

本包不覆盖旧版，主机新目录为 `r26_rs4853_uplink_demo`。设备 1、2 的 CAN、RS485 和原有上报路径保留。设备 3 没有 CAN，不能做双总线交叉验证。所有寄存器值掉电丢失。

**测试范围：**已运行本地软件模拟，覆盖设备 3 授权上报、错误标识和无应答时保持总线所有权、主机计数核对、设备 1/2 同时上报，以及网页控制逻辑。本包的新 RTL 尚未在此环境运行 Vivado 仿真、综合、烧录或实板测试。请先完成下面的 Vivado 仿真和时序检查，实板结果以实际操作为准。

## 1. 停止旧的主机演示服务

如果主机串口正在前台运行演示，先在该终端按 Ctrl+C，等待服务退出。也可执行：

```sh
/etc/init.d/S95igw_gateway stop
ps | grep -E 'igw_|run_.*gateway|yolo_demo' | grep -v grep
```

如果仍有演示进程，先停止它们的启动脚本。程序运行或板子重新配置时不要手动删除所有权锁。接线继续沿用已验证的 RS485 A0/B0 和 P1 跳帽，不需要改成 CAN。

## 2. Windows：新建 Vivado 工程并运行仿真

将压缩包解压到 `C:\IGW_ZYNQ_RS4853_UPLINK`，保留旧的 `C:\IGW_ZYNQ_RS4853`。在 Vivado 的 Tcl Console 执行：

```tcl
source {C:/IGW_ZYNQ_RS4853_UPLINK/create_vivado_project.tcl}
source {C:/IGW_ZYNQ_RS4853_UPLINK/run_simulation.tcl}
```

等仿真结束，应看到：

```text
TB_RESULT=PASS: RTU read/write, grant enable/disable, nonce replay, CRC and one-shot RS485 uplink
```

如果有 FAIL、超时或者没有 PASS，保留完整日志，先不要下载新位流。上述仿真使用缩短的分频参数，只检查逻辑，不等于实板 9600 波特率测试。

仿真通过后，在同一 Tcl Console 执行：

```tcl
source {C:/IGW_ZYNQ_RS4853_UPLINK/run_build.tcl}
```

检查 `IMPLEMENTATION_STATUS`、`SETUP_WORST_SLACK_NS` 和 `HOLD_WORST_SLACK_NS`，setup 和 hold slack 均不能为负；还需查看 DRC 和 Timing Summary。旧工程曾有未约束的外部 I/O 时序警告，本包也不能仅凭片内 slack 宣称外部接口时序已闭合。确认无阻塞性问题后，用 Hardware Manager 通过 JTAG 将**新工程**生成的 `rs485_key_led.bit` 临时下载到 `xc7z020`；不要写入 Flash。断电后 JTAG 配置会消失。

## 3. Ubuntu：编译主机程序

将压缩包放到 Ubuntu 的用户目录后执行：

```sh
igw_rs3_up_work=$(mktemp -d "$HOME/igw_rs4853_uplink.XXXXXX")
unzip -q "$HOME/IGW_ZYNQ_RS4853_UPLINK.zip" -d "$igw_rs3_up_work"
cd "$igw_rs3_up_work/IGW_ZYNQ_RS4853_UPLINK"
sha256sum -c SHA256SUMS.txt
bash host/build_on_ubuntu.sh
```

成功标志是 `AARCH64_ELF64=PASS` 和 `RS4853_UPLINK_HOST_BUILD_COMPLETE`。工具链默认使用此前的 `~/uisrc-lab-anlogic/tools/aarch64-linux/bin/aarch64-linux-gnu-`，也可用 `CROSS_COMPILE` 指定前缀。

将该目录 `host/deploy/` **里面的所有文件**复制到主机 SD 卡的 `igw_apps/r26_rs4853_uplink_install/`。路径不存在可新建；不要复制到原有 `r26_rs4853_demo` 目录中。

## 4. 安装到主机的独立目录

确认旧服务已停止后，在主机 Buildroot 串口执行：

```sh
/etc/init.d/S95igw_gateway stop
cd /mnt/mmcblk0p1/igw_apps/r26_rs4853_uplink_install
sha256sum -c SHA256SUMS.txt
sh install_on_board.sh
```

看到 `RS4853_UPLINK_HOST_STAGED=/mnt/mmcblk0p1/igw_apps/r26_rs4853_uplink_demo` 表示准备完成；安装脚本不会启动服务。它以之前已验证的 `r26_rs4853_demo` 为基线，保留原目录。若检查报错、发现进程或锁存在、或新目录已存在，应先核对现场，不要删除锁或覆盖旧目录。

## 5. 实板演示

在完成步骤 2 后，启动主机的新目录：

```sh
cd /mnt/mmcblk0p1/igw_apps/r26_rs4853_uplink_demo
IGW_NODES=3 sh run_secure_gateway.sh
```

按原来的流程在主机串口输入归档口令；不要把口令发到聊天或截图。看到服务 READY 后，在 PC 上打开 `http://192.168.31.222:8080`：

1. 选择“设备 3”，等待 RS485 在线；它没有 CAN。
2. 在“从机 → 主机”选择 RS485，填写 `54321`，点击“配置并等待从机上报”。
3. 只有页面提示主机收到新的匹配上报，且下方 RS485 上报表中设备 3 的数值和接收次数更新，才算本次上报通过。
4. 点击“恢复自动计数”，应在后续授权时隙收到设备本地运行秒数；多观察几次数值与接收次数变化。
5. 再验证设备 3 的主机写入、设备 1/2 的读写和上报，观察超时与错误计数；记录现场结果。

如果页面提示设备 3 的上报版固件未确认，检查是否下载的是本包生成的新位流，以及设备 3 的 RS485 是否在线。如果结果需复核，不要反复自动重发写命令；导出网页会话证据，并保留 `/tmp/igw_web_rs485.log`。

若时隙无有效应答或帧标识不匹配，软件可能保留 RS485 所有权，避免未知发送器和其他通信冲突。停止主机演示后，先复位或重新配置设备 3，确认无任何活动发送器，再检查主机进程和 `/tmp/igw_rs485_once.lock`；不要删除仍被使用的锁。如不确定恢复步骤，保留完整日志。

## 6. 本地软件检查与协议说明

在有 GCC、Python 3、Node.js 的 Linux 中执行 `sh run_local_checks.sh`。这些测试使用模拟 MMIO、CAN 控制、网络和从机，不控制真实设备，也不会运行 Verilog 仿真。

设备 3 使用 9600 8E1：普通读写仍为 Modbus RTU FC03/FC06。授权和上报使用本项目私有的 FC45/FC46 帧，**不是标准 Modbus 设备通用协议**。R27 取 `0xa526` 表示固定值、`0xa527` 表示运行秒数、0 表示关闭；R29 为固定值；R9=`0x0d02` 标识本包的上报能力。主机在有效的 FC03 读回确认设备身份、能力和上报模式后才发授权。每个授权带唯一标识，从机每次授权最多发送一帧。主机将收到的设备 3 数据放入独立的接收区，并计入总线帧数核对。设备本身不接收主机对该上报的确认帧；网页显示的是主机侧的接收确认。
