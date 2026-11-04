# ZYNQ-7020 设备 3：RS485 从机适配包

## 当前状态

- 目的：在原有安路主机 + 安路从机（逻辑设备 1、2）之外，增加一块 Zynq-7020 作为物理设备 3。设备 3 只使用板载 RS485；设备 1、2 继续使用原有 CAN 和 RS485。
- 此包包含新的 PL 源码、Vivado 工程创建与自检脚本、适配后的主机监控网页源码、主机编译/独立安装脚本。未修改原来的 CAN3、R26 或厂商工程，也不包含新 bitstream 或 BOOT.bin。
- 主机侧已做本机模拟测试：设备 3 的 RS485 写入和独立读回、禁止设备 3 的 CAN / 双总线交叉操作、设备 1/2 的交叉验证。模拟不代表实板结果。
- 尚需用户在 Vivado 做 RTL 仿真、综合、实现，再进行实板验证。芯片位宽、收发方向、收发器、两板接线和并发运行均以实际验证为准。

## 功能和边界

| 项目 | 设备 3 |
| --- | --- |
| 接口与站号 | 板载 RS485，Modbus RTU 地址 3，9600 bit/s、8E1 |
| 读 | FC03：R0–R31，每次 1–8 个寄存器 |
| 写 | FC06：R16–R26、R31，值 0–65535；接收后可通过读回与写计数核对 |
| 状态 | R0=0x1234，R1=3，R2=有效请求计数，R3=运行秒数，R4=0x0801，R5=写次数，R6/R7=最后写入地址/数值，R8=0x0301 |
| 主动发送 | 不支持。R2/R3 通过主机发起轮询得到，不是从机无请求主动上报 |
| 存储 | 全部寄存器易失，掉电/复位清零；不支持文件传输、SD 存储或 NPU |

设备 1、2 的真实主动上报仍使用原有机制；其中 RS485 上报依赖 CAN 时隙协调。设备 3 不具备 CAN，不应宣称支持该上报机制。网页会禁用设备 3 的 CAN、交叉验证与主动上报按钮。

## 1. 在 Windows 创建 Vivado 工程并仿真

将整个压缩包解压到一个**英文路径**，例如 `C:\IGW_ZYNQ_RS4853`。打开 Vivado（用户原厂工程为 2020.2，选择与板卡匹配的版本），在下方 **Tcl Console** 分别输入：

```tcl
source {C:/IGW_ZYNQ_RS4853/create_vivado_project.tcl}
source {C:/IGW_ZYNQ_RS4853/run_simulation.tcl}
```

若解压路径不同，请相应修改。工程创建在包内 `vivado_generated` 子目录；脚本不会覆盖已有目录。仿真运行完应明确看到：

```text
TB_RESULT=PASS: unit filtering, 8E1, read/write/readback, CRC, parity, broadcast, exceptions
```

若没有看到 PASS，或出现 `FAIL`、编译错误、仿真超时，请保留控制台完整输出；先不要生成或下载 bitstream。该仿真使用缩短的时钟分频参数以缩短等待，只验证 RTL 逻辑，不替代实际 9600 波特率电气测试。

## 2. 生成 bitstream，临时下载

仿真通过后，在同一 Vivado 工程的 Tcl Console 输入：

```tcl
source {C:/IGW_ZYNQ_RS4853/run_build.tcl}
```

脚本会跑综合、实现和位流生成，检查最差 setup slack；查看控制台的 `IMPLEMENTATION_STATUS`、`SETUP_WORST_SLACK_NS` 和 DRC/timing summary，有错误或负 slack 就先处理。生成位流后，通过 Hardware Manager 选择识别到的 **xc7z020** 设备，仅用 JTAG 的 Program Device 临时下载生成的 `rs485_key_led.bit`。不要写 Flash、BOOT.bin 或其他板卡。

这是纯 PL 设计：运行它不需要 Vitis ELF，也不会输出之前的 `CAN3 rx=...` 串口日志。JTAG 临时配置会在断电后消失。不要同时运行旧 CAN3 程序重新下载旧 bitstream。

## 3. 上电接线前核对

先将待接线设备断电。按用户提供的《领航者 ZYNQ 之 FPGA 开发指南》和 V3.9 底板原理图，P1 的 **3–5、4–6** 两组跳帽选择板载 RS485：UART2_TX ↔ RS485_RX，UART2_RX ↔ RS485_TX；引脚约束为 M15 发送、K14 接收，50 MHz 时钟 U18、复位 N16。请按板上 P1 实际编号核对。P1 的 UART2 是底板网络名，不表示 PS UART2。

设备 3 走板载 SP3485 的 RS485 端子；不要把之前测得约 4.8–4.9 V 的 CAN P3 信号直接接 Zynq 的 3.3 V IO。原理图显示 Zynq 板载 RS485 有 120 Ω 终端电阻 R4。多点总线通常仅在主干两个物理末端各配置一个约 120 Ω 终端；接入第三台前先核对原有两端及第三台是否已有终端，避免并联过多。断电时总线两线间电阻在理想的两端 120 Ω 情况下约 60 Ω，偏置网络等可能影响读数，不能只凭电阻值断言安全。参见 TI RS-485 Design Guide： https://www.ti.com/lit/an/slla272d/slla272d.pdf

RS485 的 A/B 命名在不同厂商之间可能相反，以两板原理图上的收发器引脚及信号定义核对；不要把 CAN H/L 当作 RS485。确认两板供电、隔离及共模电位满足收发器要求；如信号参考或共模范围无法确认，先不要直接连接，使用合适的隔离方案或先核对文档。先不要以之前 CAN 引脚相对板地的 4.8/4.9 V 测量来推断两板 RS485 的共模电位。

## 4. 编译与分目录安装主机网页

在 Ubuntu 将同一个包解压到英文目录，运行：

```sh
cd /解压位置/IGW_ZYNQ_RS4853
sha256sum -c SHA256SUMS.txt
bash host/build_on_ubuntu.sh
```

主机交叉编译器默认沿用此前 CAN3 包使用的 `$HOME/uisrc-lab-anlogic/tools/aarch64-linux/bin/aarch64-linux-gnu-`；如果不在该位置，用 `CROSS_COMPILE` 环境变量指定实际工具链前缀。成功后将 `host/deploy/` **里面的所有文件**复制到安路主机 SD 卡的 `igw_apps/r26_rs4853_install/`。

启动主机板，将当前前台演示服务用 Ctrl+C 正常停止，再在主机串口执行：

```sh
/etc/init.d/S95igw_gateway stop
cd /mnt/mmcblk0p1/igw_apps/r26_rs4853_install
sha256sum -c SHA256SUMS.txt
sh install_on_board.sh
```

脚本会检查原版 `r26_bidir_ui` 和活动服务，另建 `/mnt/mmcblk0p1/igw_apps/r26_rs4853_demo`；不覆盖原版。若目标目录已存在，脚本会停止，请保留现场输出，不要删除已有版本或锁文件。仅安装，不会自动启动服务。

设备 3 还没准备好时，可以仅测试原设备：

```sh
cd /mnt/mmcblk0p1/igw_apps/r26_rs4853_demo
IGW_NODES=2 sh run_secure_gateway.sh
```

设备 3 已完成上面的仿真、位流配置与电气核对后，再用 `IGW_NODES=3 sh run_secure_gateway.sh` 开始三逻辑设备演示。服务解锁等流程沿用原版，测试完 Ctrl+C 正常退出。

## 5. 实板验收建议

在 PC `192.168.31.100` 打开主机监控网页 `http://192.168.31.222:8080`，选择设备 3：CAN 显示未接入，RS485 在线；R0=4660、R1=3、R8=769，R2/R3 随轮询或时间变化。选择 RS485，在 R16 写入 12345，观察写入及独立读回通过；再查看 R5/R6/R7。选择设备 1、2，确认 CAN、RS485 与原有交叉验证仍可正常工作，并观察从机上报、超时与错误计数。

在实板完成之前，以上都是预期现象，不是已取得的验收结论。此包也没有证明 RS485/CAN 无丢包、长期并发或小于 50 ms 转发延迟。

## 6. 开发者本机检查

在具备 GCC、Python 3、Node.js 的 Linux 环境中，运行 `sh run_local_checks.sh`。这只启动本机的模拟传输和网页测试，不访问真实 FPGA、串口、CAN 或 RS485，也不运行 Verilog 仿真。
