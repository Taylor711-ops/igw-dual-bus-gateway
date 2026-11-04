# IGW CAN + RS485 业务读写 R9

目标：在已通过 RS485 R8 硬件测试的基础上，加入 CAN 业务读写，共用从机寄存器，验证 CAN 写→RS485 读以及 RS485 写→CAN 读。源代码包不含新生成的目标板可执行文件或 BOOT.bin；Linux 程序需在原 Ubuntu 交叉编译环境构建，从机 main.c 需在原 FD 从机应用构建。

## 本次范围与保持项

- 两块板、原接线；主机 Linux 192.168.31.222，PC 192.168.31.100。
- 主机继续用已经验证的 R2 SD BOOT，SHA256 为 `7b81535086c3096ded1e15061118236978730d56f9132d2616bc573cc8dd734c`。**不覆盖主机 BOOT.bin，不修改 PL、时钟约束或根文件系统。**
- 从机继续用原 100MHz RS485 硬件平台 `igw_can_plat`，更新原 RW_R8 从机应用的 main.c。**不要把此 main.c 放入 Linux 主机应用。**
- CAN 仍是芯片 PS 集成控制器，通过 SocketCAN/SDK 使用；本包没有实现新的 PL CAN 控制器或完成 PL 双总线评分项。
- RS485 Linux 网关 `src/igw_rs485_rw.c`、`src/rw_wire.h`、原 PC R8 工具保持字节不变；FC03/FC06 协议与寄存器4的 0x0801 兼容。
- CAN 500k，RS485 9600 8E1；没有自动重试写入。所有写操作仅修改从机易失 RAM 测试寄存器。
- 本包是单逻辑节点请求/应答读写，不含从机主动推送、多设备、网页、加密存储、NPU集成或全设计时序签核。

## 1. 从机：先更新应用

1. 先停止 PC 发送。主机 R8 启动脚本窗口按 Ctrl+C，使其正常停止两个网关。
2. 备份现有 RW_R8 从机应用的 main.c 和可恢复启动镜像。
3. 将本包 `slave/main.c` **只覆盖原 FD 从机应用的 main.c**。沿用原 `igw_can_plat` 及原从机 PL；不要将 tests/src/slave_src 全部加入 FD 工程。
4. 按之前成功的流程重新构建从机应用、生成并下载从机启动镜像，再冷启动从机。
5. 从机串口应出现：

```
[IGW-DUAL] gateway RW_R9 role=SLAVE ...
[DUAL-SLAVE] READY ... RW_R9 shared registers16..31 volatile; CAN RW request=0x211 reply=0x191
```

这一步仅证明新程序启动；业务验证以 PC 原始记录为准。若构建/启动不符，先保留完整文本输出，不连续发送写命令。

## 2. Ubuntu：构建 Linux 两个程序

将整个源代码包复制到之前可用的 Ubuntu 交叉编译环境，进入包根目录执行：

```bash
bash build.sh
```

默认工具链位置沿用 R8：`$HOME/uisrc-lab-anlogic/tools/aarch64-linux/bin/aarch64-linux-gnu-gcc`。若实际安装位置不同，仅通过 CROSS_COMPILE 指定正确前缀。脚本检查源码哈希、编译两个静态 AArch64 Linux ELF，并输出 readelf 头。

完成后把 `deploy` 目录内以下 **5 个文件一起**复制到 Windows `C:\igw_can_rs485_rw_r9`：

| 文件 | 用途 |
|---|---|
| igw_can_udp | R9 CAN 旧接口+业务读写 |
| igw_rs485_rw | 保持 R8 的 RS485 读写网关 |
| run_rw_gateway.sh | R9 两网关启动/停止 |
| build_info.txt | 编译与源码哈希 |
| SHA256SUMS.txt | 上述4文件校验 |

不要混用 R8 的 igw_can_udp 或校验文件。源代码包中的 deploy 文件夹在执行 build.sh 前没有目标 ELF 和部署 SHA256SUMS.txt。

## 3. Windows → Linux 主机

Windows 打开一个 CMD，保持 HTTP 服务运行：

```bat
E:\python3.11\python.exe -m http.server 8013 --bind 0.0.0.0 --directory C:\igw_can_rs485_rw_r9
```

主机 Linux 串口执行以下一整块。`&&` 保证下载/校验失败后不会继续启动。不要带上提示符 `#`。

```sh
mkdir -p /tmp/igw_can_rs485_rw_r9 &&
cd /tmp/igw_can_rs485_rw_r9 &&
wget -O igw_can_udp http://192.168.31.100:8013/igw_can_udp &&
wget -O igw_rs485_rw http://192.168.31.100:8013/igw_rs485_rw &&
wget -O run_rw_gateway.sh http://192.168.31.100:8013/run_rw_gateway.sh &&
wget -O build_info.txt http://192.168.31.100:8013/build_info.txt &&
wget -O SHA256SUMS.txt http://192.168.31.100:8013/SHA256SUMS.txt &&
sha256sum -c SHA256SUMS.txt &&
chmod +x igw_can_udp igw_rs485_rw &&
sh run_rw_gateway.sh
```

保持该窗口运行。预期 `RW_R9 READY: CAN legacy+read/write UDP15000; RS485 read/write UDP15001.`

脚本先检查身份和占用，再停止系统自动启动的旧 CAN 服务。若还有 R8/其他手动网关正在运行，会拒绝启动；在原窗口 Ctrl+C 停止其所属启动脚本后再启动 R9。**不要删除活动锁或同时运行两个总线发送者。**

日志为 `/tmp/igw_rw_can.log` 和 `/tmp/igw_rw_rs485.log`。Ctrl+C 正常停止自己启动的两个进程，检查 RS485 TX授权为0。

## 4. PC：只先跑一轮 CAN

解压源代码包，进入 `pc` 文件夹（里面必须能看到 `can_rw_test.py` 和 `rs485_rw_test.py`），在资源管理器地址栏输入 `cmd` 回车。

```bat
E:\python3.11\python.exe can_rw_test.py --mode can --address 16 --count 1
```

或者双击 `start_can_rw.cmd`。新从机寄存器8必须为0x0901；旧从机不会通过该检查。该轮读取写前值/元数据、写不同值、独立 CAN 读回、核对共享写计数+1。成功应显示 `RESULT=PASS_CAN_WRITE_READBACK`。

发回生成的 `can_rw_*.jsonl`，确认一轮后再用：

```bat
E:\python3.11\python.exe can_rw_test.py --mode can --address 31 --count 10
```

## 5. 再验证两个方向的跨协议共享数据

CAN 一轮通过后，先执行：

```bat
E:\python3.11\python.exe can_rw_test.py --mode cross --address 16 --count 1
```

该轮依次：读取双方同一旧值→CAN写新值→RS485独立读回→核对写计数+1→RS485再写另一个值→CAN独立读回→核对总写计数+2。

成功显示 `RESULT=PASS_CROSS_PROTOCOL_READBACK`，随后按 10轮、100轮推进：

```bat
E:\python3.11\python.exe can_rw_test.py --mode cross --address 31 --count 100
```

每轮间隔1秒。cross每轮4次CAN、6次RS485；can模式每轮12次CAN。只运行一个 PC 测试程序，关闭旧测试面板和轮询器。**这证明同一节点的数据可经不同协议写入和读出，不代表主机已经具备任意报文路由、从机主动上报或多设备管理。**

任何超时/不一致立即停止，不自动重发写操作；写超时可能已经改变从机寄存器，不能据超时认定未执行。先保存 JSONL 和两端日志再诊断。

## 6. 持久备份与重启恢复

部署校验通过后，在主机另一个可用终端将5文件备份 SD（不修改 BOOT）：

```sh
mkdir -p /mnt/mmcblk0p1/igw_apps/can_rs485_rw_r9 &&
cp /tmp/igw_can_rs485_rw_r9/igw_can_udp /tmp/igw_can_rs485_rw_r9/igw_rs485_rw /tmp/igw_can_rs485_rw_r9/run_rw_gateway.sh /tmp/igw_can_rs485_rw_r9/build_info.txt /tmp/igw_can_rs485_rw_r9/SHA256SUMS.txt /mnt/mmcblk0p1/igw_apps/can_rs485_rw_r9/ &&
sync &&
(cd /mnt/mmcblk0p1/igw_apps/can_rs485_rw_r9 && sha256sum -c SHA256SUMS.txt)
```

重启会清空 `/tmp`。恢复命令：

```sh
mkdir -p /tmp/igw_can_rs485_rw_r9 &&
cp /mnt/mmcblk0p1/igw_apps/can_rs485_rw_r9/* /tmp/igw_can_rs485_rw_r9/ &&
cd /tmp/igw_can_rs485_rw_r9 &&
sha256sum -c SHA256SUMS.txt &&
chmod +x igw_can_udp igw_rs485_rw &&
sh run_rw_gateway.sh
```

## 7. 寄存器与私有 CAN 帧

| 地址 | 内容 | 权限 |
|---|---|---|
|0|0x1234|只读|
|1|逻辑节点1|只读|
|2|RS485请求累计数低16位|只读|
|3|从机运行秒数低16位|只读|
|4|R8兼容寄存器版本0x0801|只读|
|5|CAN与RS485共用写次数，uint16回绕|只读|
|6/7|最近一次接受写操作的地址/值|只读|
|8|CAN业务版本0x0901|只读|
|9..15|保留0|只读|
|16..31|易失RAM测试值，启动为0|CAN/RS485读写|

CAN使用标准数据帧、DLC8。私有请求ID0x211、回复0x191，节点1隐含在ID和服务配置中。不是CANopen协议。原0x201/0x181固定测试仍兼容。

字节0..3为uint32事务序号小端；字节4请求READ=1/WRITE=2，成功回复0x81/0x82；字节5寄存器地址；字节6..7为uint16值大端，读请求必须为0。CAN自带链路CRC，本业务帧未另加Modbus CRC。错误回复字节4为E1格式/E2读取越界/E3只读写入/E4序号冲突/E5写缓存满，值字段0。

Linux业务命令：

```
IGW2 CAN READ <seq_u32> 1 <address_0..31>
IGW2 CAN WRITE <seq_u32> 1 <address_16..31> <value_0..65535>
```

严格十进制、单空格、无换行；没有读count字段。RS485仍使用R8语法、FC03/FC06。

Linux CAN 保存1024个60秒内未过期的请求结果，按源IP/源端口/seq和命令匹配；同序号不同命令拒绝，缓存满拒绝新请求，不淘汰未过期项。从机保存128个CAN成功写命令60秒，同seq不同命令拒绝，重复写返回原ACK且不重复修改RAM。读取不在从机写缓存中。原RS485网关保留512项缓存。按配套工具1秒轮间隔可运行100轮；不要移除间隔进行高频灌入，容量耗尽会明确拒绝。

这些是单进程/单次从机运行内的短期防重。重启、超过60秒或更换源端口后的Linux缓存不提供持久事务保证；从机仅对仍在写缓存内的CAN序号防重。**不能据此宣称端到端永久恰好执行一次。**

CAN超时沿用原专用接口down/up取消挂起发送，冷却2秒；不要在其他应用共享CAN接口时运行。写应答ACK不等于状态读回，PC必须独立读回并核对元数据才能计PASS。

## 8. 本地验证与还未验证的内容

- `python3 tests/run_checks.py`：原CAN/RS485回归、真实C UDP事件循环与从机协议仿真、新读写/交叉读回、缓存防重/拒绝、错误/丢失应答、PC CLI、启动脚本。
- 生成的完整从机main.c在宿主桩函数中运行，ASan/UBSan检查；验证原100+100并行、两总线隔离超时、新CAN业务读写与两方向共享寄存器、重复CAN写不重复计数。
- 宿主功能测试正常CAN截止时间500ms、专门故障用例80ms，用于隔离宿主调度抖动；实际主机部署仍为原250ms，必须以板上记录验收。
- 快速跨协议仿真80轮=320 CAN事务+480 RS485事务，不人为超出原RS缓存512的无等待突发容量；实际PC100轮使用1秒轮间隔。
- `python3 check_slave_sdk.py --sdk /实际/igw_can_plat`：使用真实SDK头文件做语法和类型检查；不是目标ARM链接。
- 测试结果见 `VALIDATION.json` 和 `evidence/`。不将主机仿真延迟当作板上性能。
- R8硬件已知基线：寄存器16十轮成功、31一百轮成功，原始CRC/应答/计数已核验；**R9生成之后尚需用户完成目标构建及硬件验收**。

本地通过后仍需：两板CAN一轮→跨协议一轮→批量与故障恢复→明确主动上报/多设备需求→网页与加密存储→中级验收。系统全设计时序问题和PS CAN架构评分范围仍应在最终报告中如实记录。
