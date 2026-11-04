# R15 网关精简工程：先编译与核对 Linux，再制作启动镜像

这份工程用于完成中级目标剩余的 PL CAN 接收解析验证，并清除原参考设计中无关的视频时序路径。中级要求没有 HDMI；Web 页面继续作为监控界面。现有 R13 应用、SD 启动文件和从机均保留作回退。本包是源代码候选版，没有可直接烧写的 BOOT.bin 或 bitstream。

## 1. Windows 打开工程

1. 将压缩包解压到 `C:\igw_r15`，确认下面这个文件真实存在，不要多嵌套一层同名目录：

   `C:\igw_r15\IGW_GATEWAY_R15_CORE\project_r15\soc_prj\soc_prj.al`

2. 双击 `project_r15\check_project.cmd`，需要 Windows Python 3。正常结束标记：

   `R15_SOURCE_AND_WIRING=PASS`

3. 在原来的 TD 2026.1 SP2 中打开上述 `soc_prj.al`。确认器件为 **DR1M90GEG400，速度等级 -2**，顶层为 `system_top`。
4. 运行 **Synthesis**，成功后运行 **Implementation / Physical Design**，直到布局布线及最终时序报告完成。具体按钮名称沿用你安装的 TD。
5. 保存本次完整编译日志、最终 setup/hold 时序报告，以及未约束路径/时钟检查结果。把这些文件发回，出现首个 ERROR 则先发完整上下文。

这是固定 HDL 加独立 IP 的工程，保留的 IP 源码随包提供，不需要重新生成旧 `design_0` 图形化系统。不要把 R14 工程的 HDMI FIFO、PWM 例外或旧视频约束复制进来。旧 IP 名称中的 `Video_NPU_0` 是需要保留的 NPU 模块，并非漏删的视频输出。

## 2. 同时采集当前主机 Linux 信息

精简硬件去掉了旧视频寄存器。必须先确认当前启动设备树没有继续启用对应视频驱动；硬件返回 AXI DECERR 可以结束总线事务，但不能保证旧 Linux 驱动不会因访问已移除设备而报错。**本步骤只采集信息，暂不替换 BOOT.bin，也不在运行中的板上下载新 bitstream。**

在 PC 新开一个命令提示符，运行：

```bat
cd /d C:\igw_r15\IGW_GATEWAY_R15_CORE
py -3 -m http.server 8016 --bind 192.168.31.100
```

保持这个窗口运行。若 Windows 提示防火墙，允许本次局域网连接。确认 PC 以太网仍是 `192.168.31.100`。

在 **Linux 主机板** 的 `#` 提示符下逐行执行，下述命令不用在裸机从机上执行：

```sh
wget -O /tmp/collect_linux_video.sh http://192.168.31.100:8016/linux/collect_linux_video.sh
sh /tmp/collect_linux_video.sh
```

如果主机终端正在运行安全网关且尚未返回 `#`，可另开一个已有权限的主机登录会话执行；也可先按一次 Ctrl+C 等待最终加密归档及 `SECURE_SERVICE_STOPPED` 后再执行。不要把运行日志当作命令粘贴。

把完整输出及 `EVIDENCE_ARCHIVE=...tar` 指向的归档发回。归档含运行设备树、平台驱动与启动目录清单，不含口令、解密后的业务 JSON 或新启动镜像。不要使用 `tar -z`，此板 BusyBox 不支持该选项。当前阶段无需发送 CAN/485 测试流量。

拿到设备树后，再针对实际启动方式修改视频节点状态，并完成与新 FPGA 工程匹配的启动包。不能仅凭 SD 上 BOOT 文件的 SHA 判定当前正在运行的硬件版本。

## 3. 具体保留与移除内容

| 项目 | R15 状态 |
| --- | --- |
| PS 配置、DDR、以太网、原 PS CAN 引脚 | 保留原 IP 配置 |
| PL RS485、AXI 分支、PL CAN 被动接收解析/FIFO | 保留原 R14.1 总线 RTL，7 个叶级源码逐字节相同 |
| NPU GP1/HP0、控制/数据连接、复位及 IRQ | 保留原连接，未改频率 |
| 原视频链路占用的 HP1 | 原时钟保留，已移除主设备的输入置零 |
| HDMI 输出、MIPI 接收、ISP、视频 DMA、视频时序、PWM 演示逻辑 | 移除 |
| 原视频及其他未映射 GP0 地址 | 显式 AXI-Lite DECERR，不模拟成功访问 |
| I2C 与三只 LED | 保留原管脚；LED0 心跳，LED1 网关复位释放，LED2 原 PLL 锁定；LED2 不表示推理活动 |

RS485 地址仍为 `0x80400000`，被动 CAN 观察窗口为 `0x80400800`。PL CAN 仍然只监听，不负责总线 ACK、发送或控制 PS CAN TX。CAN 主从读写继续由既有 PS CAN 通路执行。此包没有新增压缩、4G、OTA 或故障预测功能。

主网关和软 NPU 的原 AXI 时钟仍接 PS `p2f_clk0`（50 MHz）。保留名为 `PLL_npu` 的 PLL 是为了维持原锁定/复位依赖，不能把它的名称当作 NPU 核心实测频率。

## 4. 本地完成的验证与仍需完成的验证

已完成：

- 13 个保留 IP 的 86 个叶级文件与输入版本一致；顶层接口按实际厂商端口声明检查通过。
- 新地址译码/错误响应测试 1,448 项检查通过，覆盖 AW/W 到达先后、读写背压、复位、33 位高地址不串址、RS485 配置与 TX 授权不被错误地址改变。
- 原 CAN/RS485 分支、AXI、双板收发仿真与默认 9600 配置测试通过。
- CAN 串行接收 305 个有效帧、10 组畸形帧与相关位填充边界测试通过。
- 新错误响应模块和总线分支通用综合结构检查通过；主机解码器 ASan/UBSan 检查通过。
- 错时钟、删除 NPU、管脚冲突、截断地址的源代码变异均能被检查器拒绝。

厂商加密 IP 本地检查仅使用实际端口声明做接口检查，没有模拟其内部硬件行为。通用综合检查不等于 TD 厂商布局布线。

仍需：**TD 综合/布局布线、最终时序及约束覆盖检查、实际 Linux 设备树处理、新启动包冷启动、PL CAN FIFO 实板比对、CAN+RS485+NPU+Web+加密归档联合回归。**这些通过前不能宣称中级整体验收完成。两个逻辑节点也不能直接当作两个独立物理从设备的验证。

## 5. 文件用途

| 路径 | 用途 |
| --- | --- |
| `project_r15/soc_prj/soc_prj.al` | 本次 TD 工程入口 |
| `project_r15/check_project.py` | 只读源码、连接、依赖和约束检查 |
| `audit/integration.json` | 保留/移除实例和连接审计 |
| `linux/collect_linux_video.sh` | 当前 Linux 设备树与视频驱动采集 |
| `validation/r15_checks.json` | 本地检查结果；TD/设备树/实板状态明确为 false |
| `host/` | 新硬件验证后的 CAN 被动 FIFO 采集源码，尚未构建目标板二进制 |
| `tests/`、`project_r15/tests/` | 仿真与回归测试源码 |
| `PACKAGE_SHA256SUMS.txt` | 本包交付文件校验表，不是 BOOT 镜像 SHA |

`host/guarded_capture.sh` 仅在后续确认 R15 实际启动镜像后使用，要求 `R15_BOOT_SHA256` 和 `R15_COLD_BOOT_CONFIRMED=YES`。脚本中的 R14 字样/寄存器 ABI 是被动 CAN 观察器沿用的版本，不代表本包已经在板上运行。当前阶段不执行该采集程序。

若需要在装有 Icarus Verilog、Python 3、C 编译器和 Yosys 的 Linux 开发机复跑本地测试：

```sh
python3 tests/run_core_checks.py --iverilog /usr/bin/iverilog --vvp /usr/bin/vvp --yosys /usr/bin/yosys
```

验收优先级：先完成中级总线硬件与联合回归，再推进高级 PL 打包压缩、三总线融合和设备预测；不再为本题增加 HDMI 显示工作。
