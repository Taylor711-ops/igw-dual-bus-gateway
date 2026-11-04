# R16：设备主动写入网关（第一阶段：CAN）

这是可编译源码包，已完成本地测试，尚未完成两块目标板上的部署验证。
本包不含目标 ELF 或新 BOOT.bin。

## 实现内容与边界

保留 R15 硬件、DTB FIX1、原 CAN/RS485 读写与 CROSS、冻结 NPU 模型和 AES-GCM 归档。
新增从机每秒主动产生测试计数，通过独立 CAN ID 写入每设备独立的主机接收区 R16。
主机接收区 R16 与从机共享寄存器 R16 是不同的数据区；从机 R11 显示当前生成的测试计数。
主机更新状态及易失快照后才提交应用层确认；同一会话和序号的重复报文确认但不重复计数。
支持会话重建、序号用尽前重建、过期数据拒绝、最多三次尝试和明确的 unknown 状态。

这是最新状态同步，数据是设备内部生成的测试计数，不是实测传感器数据。
不用于需要跨掉电“恰好执行一次”的执行器控制，不提供设备身份认证，CAN 线路本身未加密。
**RS485 主动上传尚未实现**，目前仍为原 Modbus RTU 轮询。
两个设备是同一块从机板模拟的独立地址；不代表两台物理从机验证。
NPU 模型未修改，仍仅分析设备 1 的原通信特征；不声称故障预测或分类准确率已验收。

## 1. Ubuntu 虚拟机编译主机程序

解压到用户主目录后执行：

```sh
cd ~/IGW_R16_CAN_UPLINK_SOURCE
bash build.sh
```

默认沿用 ~/uisrc-lab-anlogic/tools/aarch64-linux/bin/aarch64-linux-gnu-gcc。
只编译新 Web 和主动上传接收器，不编译 ArmNN、内核、FSBL 或 FPGA。
看到 BUILD_COMPLETE_R16 后，把整个 deploy 文件夹复制回 Windows。

## 2. 编译准备从机应用

把 `slave_r16/slave/main.c` 替换到当前运行 R13 的**从机裸机应用工程 main.c**。
使用从机原有 SDK/链接脚本编译，准备好原下载或 QSPI 应用打包结果；此时先不要刷写或复位从机。完成第 3 步主机安装，并启动第 4 步接收器后再更新从机，避免单边运行时 CAN 缺失硬件 ACK。
保留从机原有 FPGA 位流和平台，不将主机 R15 BOOT.bin 或位流刷到从机。
这个 main.c 不放入 Linux 主机的 deploy 目录。

启动应显示 `gateway UPLINK_R16 role=SLAVE`、`R16 READY`，以及：

```text
[UPLINK] R16 autonomous CAN state writes to host R16 per node; generated test counters, 1s period, ACK/retry; RS485 remains polled
```

主机未启动时 HELLO 重试正常，不会等待确认而阻塞原双总线服务。
生成文件默认逻辑节点 1、2，重新生成可运行 `python3 slave_r16/generate_slave.py`。

## 3. 主机独立安装

主机执行 sync、poweroff，关机后断电取卡。
将 deploy 内**全部文件**复制到主机 SD 卡 `igw_apps/r16_can_uplink_install/`。
保留已验证的 BOOT.bin、system.dtb 及原 igw_apps/r15_bringup。
上电登录后：

```sh
/etc/init.d/S95igw_gateway stop
cd /mnt/mmcblk0p1/igw_apps/r16_can_uplink_install &&
sha256sum -c SHA256SUMS.txt &&
sh install_on_board.sh
```

安装器验证原 R15 清单后，复制原 CAN/RS485/vault/NPU/采集程序，并安装新 Web/上传接收器到独立 r16_can_uplink 目录。原 R15 保留不变。

## 4. 先证明设备上传不依赖轮询

保持原接线，关闭旧 PC UDP 面板。从机可暂时运行原 R13，先启动主机接收器。

```sh
cd /mnt/mmcblk0p1/igw_apps/r16_can_uplink &&
sh run_uplink_only.sh
```

此模式**不启动 Web、NPU、CAN 请求网关或 RS485 轮询**，只运行上报接收/确认器和被动 PL 采集。
看到主机 UPLINK-ONLY READY 后，按原 SDK 下载/QSPI 更新流程刷入已编译好的从机 R16 应用并复位从机。确认从机 R16 READY 后再等 20 秒，Ctrl+C 停止主机接收器，再执行：

```sh
tail -8 /tmp/igw_r16_uplink.log
cat /tmp/igw_r16_uplink.json
cat /tmp/igw_r16_uplink_only_pl.stats
```

两节点 accepted 应增加，从机日志 acked 应增加；硬件 CAN ACK 不等于应用层确认。
停止后 JSON 的 running=false 是正常标记；数据暂留 /tmp，重启后消失。

## 5. 完整系统并行验证

确认上一步结束后启动：

```sh
cd /mnt/mmcblk0p1/igw_apps/r16_can_uplink &&
sh run_secure_gateway.sh
```

私人归档密码仅在串口提示处输入，不要发送到聊天。
PC 打开 http://192.168.31.222:8080，必要时 Ctrl+F5 刷新。
页面标记 R16，新增“设备主动写入主机 · CAN”表，值和接收次数来自主机接收区，不来自网页轮询从机。
节点 1、2 各做一次原 CROSS，运行 60–90 秒后先导出 JSON，再 Ctrl+C。

```sh
tail -8 /tmp/igw_r16_uplink.log
cat /tmp/igw_r15_plcan.stats
tail -5 /tmp/igw_web_can.log
tail -5 /tmp/igw_web_rs485.log
devmem 0x8040000C 32
devmem 0x1f0000020 32
```

完整启动器管理六个子进程，PL 采集上限 600 秒，到限即清理本轮服务；不是长期生产启动器。
Web 的 uplink 字段随原快照进入 AES-GCM 归档；原始诊断帧/日志/快照只写易失 /tmp，不自动写明文到 SD。

## 协议与限制

经典标准 CAN、DLC=8、500 kbit/s，unit 1–16：

| 方向 | ID | 数据 |
|---|---|---|
| 设备 HELLO | 0x300+unit | cookie32LE、magic32LE=0x31554749 |
| 主机 WELCOME | 0x340+unit | cookie32LE、host epoch32LE |
| 设备 DATA | 0x380+unit | epoch32LE、seq16LE、value16BE |
| 主机 ACK | 0x3c0+unit | 完整回显 DATA |

WELCOME 全零表示要求重建会话。会话号区分协议状态，不提供身份认证。
三次尝试后未获确认记录 unknown；状态同步在重建后可能重发最新值，不提供跨掉电/进程崩溃的全局恰好一次语义。
Linux CAN 接口因原网关超时恢复而 down/up 时，上报接收器会尝试重绑 socket 并保留内存接收区；实际物理断线/恢复仍待板上验证。

## 本地验证和参考资料

运行 `python3 tests/run_r16_checks.py`，覆盖协议、实际 C 接收/HTTP/证据、实际生成的从机调度器、旧双总线业务和网页组件；不访问真实 CAN/MMIO。
REFERENCE_R13_*、README_R13_BASELINE.md 等仅为旧基线记录，不代表 R16 测试结果；以 VALIDATION_R16.json 为准。
逐帧 PL/PS 对齐、量化转发时延、RS485 主动上传和中级最终验收仍需后续完成。
