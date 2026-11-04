# R17：CAN + RS485 设备主动上报

可编译源码包。R17 新增功能已完成本地模拟测试，尚未在目标板验收。
本包不含 AArch64 ELF、BOOT.bin 或 FPGA 位流；继续使用已验证 R15 BOOT 和 DTB FIX1。

## 功能与真实边界

- 保留 CAN 主动上报（每设备主机影子寄存器 R16）。
- 新增从机每 2 秒自主生成测试计数，通过 CAN 申请 RS485 时隙，再经 RS485 专用 CRC 帧写入每设备主机影子寄存器 R17。
- CAN 的申请报文不含计数值；主机必须实际接收并校验 RS485 数据才提交该值。提交快照后才通过 CAN 发送数据确认。
- 现有 CAN/Modbus FC03+FC06 读写、CROSS 和两节点独立共享寄存器继续保留。RS485 新上报和原读写由同一个程序串行调度，一个 MMIO 所有者。
- Web 新增 RS485 主动上报表；rs485_uplink 字段进入原有加密归档。
- NPU 模型、特征、运行程序、AES-GCM 实现未更改；NPU仍只分析设备1的原通信特征，并非新增故障预测验收。

**这依赖 CAN 协调，不能在仅有一条标准 Modbus RTU 链路、普通从机不改固件时使用。**
从机自主产生上报意图；主机仅分配总线使用权，并非轮询寄存器取得这些上报值。
数据是测试计数，非真实传感器采样；两台设备仍为一块从机板中的两个逻辑地址。
主机 R16/R17 是不同的上传影子区，不是从机共享寄存器 R16/R17。从机只读 R11、R12 分别反映两条上传源计数。
通信线上的报文没有加密或身份认证；AES-GCM保护的是原本的本地归档。
最新状态同步、会话内去重；不保证跨崩溃/掉电恰好一次，也不保证每秒的每个中间计数均被保存。

## 1. Ubuntu 虚拟机编译主机程序

将整个源码目录解压到用户主目录，执行这两条（不是在板卡 Buildroot 终端）：

```sh
cd ~/IGW_R17_RS485_UPLINK_SOURCE
bash build.sh
```

默认使用既有工具链 `~/uisrc-lab-anlogic/tools/aarch64-linux/bin/aarch64-linux-gnu-gcc`。
只编译 Web、CAN上传接收器、扩展的RS485所有者；不编译ArmNN、内核、FSBL或FPGA。
看到 `BUILD_COMPLETE_R17` 后，复制整个 deploy 文件夹回 Windows。

## 2. 准备从机应用

将 `slave_r17/slave/main.c` 替换当前从机裸机工程的 main.c，按原SDK流程编译。
生成文件已包含需要的协议代码，不需要额外添加头文件。
保留从机原位流和平台；不要给从机写入主机 R15 的 BOOT 或位流。
先编译准备好，待第4步主机接收器READY后，再按原来的应用下载/QSPI流程更新从机。
启动标记应为 `gateway UPLINK_R17 role=SLAVE` 和 `R17 READY`。

## 3. 安装到主机独立目录

先停止旧服务、sync并正常关机后取卡。
把 deploy 内全部文件复制到主机 SD 卡 `igw_apps/r17_rs485_uplink_install/`。
保留现有 `igw_apps/r16_can_uplink`，安装器需要其中已验证的CAN、vault、NPU和PL采集二进制。
主机启动登录后，逐条执行：

```sh
/etc/init.d/S95igw_gateway stop
cd /mnt/mmcblk0p1/igw_apps/r17_rs485_uplink_install
sha256sum -c SHA256SUMS.txt
sh install_on_board.sh
```

新目录为 `/mnt/mmcblk0p1/igw_apps/r17_rs485_uplink`；安装器不覆盖旧R16目录。
BOOT.bin、system.dtb、boot.scr、主从FPGA位流均不替换。
如果新目录已存在，安装器停止，不擅自覆盖。

## 4. 先做无Web、无普通读写轮询的独立上传测试

保留主从之间 CAN、RS485 接线；不需要增加任何硬件。
关闭旧PC面板，从机可暂时继续R16，先在主机运行：

```sh
cd /mnt/mmcblk0p1/igw_apps/r17_rs485_uplink
sh run_uplink_only.sh
```

主机出现 `R17 UPLINK-ONLY READY` 后，再更新并启动从机R17。不要重启主机。
这一模式只运行CAN上报接收器、RS485受控上报所有者和被动PL CAN采集；不启动Web、NPU或普通轮询客户端。
RS所有者虽绑定本机UDP端口，但没有客户端时不发出任何普通Modbus读写请求；所有有效值来自设备生成的上报。
等从机READY后再运行20–30秒，Ctrl+C停止主机，逐条收集：

```sh
tail -6 /tmp/igw_r16_uplink.log
tail -8 /tmp/igw_r17_rs485_uplink.log
cat /tmp/igw_r17_rs485_uplink.json
cat /tmp/igw_r16_uplink_only_pl.stats
devmem 0x8040000C 32
devmem 0x1f0000020 32
```

预期两个节点的RS accepted增加、从机 `[RS-UPLINK] acked` 增加；`tx authorization=0`、NPU能力BF。
停止后running=false是正常标记。日志与影子区易失，重启丢失。
**先审核这一步结果，再进行完整联调。**

## 5. 完整系统联调（独立测试通过后）

```sh
cd /mnt/mmcblk0p1/igw_apps/r17_rs485_uplink
sh run_secure_gateway.sh
```

口令只在终端提示处输入。PC打开 http://192.168.31.222:8080，Ctrl+F5。
确认R17、CAN和RS485两张上传表都更新；设备1/2各在地址16做一次“双向交叉验证”。
运行2分钟，先导出会话JSON再Ctrl+C。
检查普通CAN/RS事务无超时、新增PL错误为0、两个主动上传通道计数增长。
用本轮打印的**具体加密文件名**执行 igw_vault verify，再open到/tmp；不要按FAT文件时间猜最新存档。
解密JSON应有software_revision=R17_RS485_UPLINK和schema=IGW_RS485_UPLINK_R17。
归档本身在SD加密存放；原始诊断/影子区仅在/tmp，不自动落明文SD。
启动器管理6个服务，PL采集上限600秒，到限清理；不是生产长期守护程序。

## 时隙、故障与安全释放

CAN经典标准帧，500k，DLC8，unit1..16。RS485 9600 8E1。

| 报文 | CAN ID | 8字节内容 |
|---|---|---|
| 从机申请RTS | 0x400+unit | CAN会话epoch32LE、RS序号16LE、magic=0x1752；不含值 |
| 主机授权GRANT | 0x440+unit | 主机nonce32LE、CAN会话epoch32LE |
| 数据提交ACK | 0x480+unit | nonce32LE、序号16LE、值16BE |
| 从机静默DONE | 0x4c0+unit | nonce32LE、序号16LE、magic=0x1744 |
| 主机释放确认 | 0x500+unit | nonce32LE、序号16LE、magic=0x1745 |
| 主机取消CANCEL | 0x540+unit | nonce32LE、序号16LE、magic=0x1743 |

RS数据帧16字节：unit、专用功能码0x41、nonce32LE、epoch32LE、seq16LE、value16BE、Modbus CRC16LE。
它不是标准FC03/FC06报文，第三方普通Modbus设备不会自动支持。
nonce随机播种递增；授权绑定CAN会话，已释放nonce按序拒绝旧授权。RS序号独立；序号用尽前重建CAN会话。
设备最多3次申请尝试，数据保持不变；未确认记录unknown。
主机按节点轮转授权，普通读写与上传交替获得服务；一个上传时隙只能发送一帧。
设备收到授权25ms内才可启动TX；80ms上限后撤销授权；迟到授权/取消都有退休nonce防重复发送。
主机200ms未完成则发取消，但**不会仅凭超时恢复普通RS485轮询**：必须收到对应DONE，等待PL帧结束/静默，再确认释放。
因此CAN控制通道失效时，RS485会暂停而非允许相互抢发。该依赖是设计约束。
停止服务时尝试取消和静默握手；如果从机已断电或CAN不可用，无法确认静默则保留RS485所有权锁并报告REVIEW。
此时先停止/复位从机、确认其发送器静默，再处理恢复；不要直接删活跃锁或同时启动其他发送器。

## 本地验证与板上验收边界

运行 `python3 tests/run_r17_checks.py`：实际C RS所有者、C HTTP/证据、生成从机调度器、CAN旧功能、CROSS/Web/vault和启动清理回归。
CAN/MMIO使用本地替身；ASan/UBSan仅代表本机代码检查，不代表板上实时性或线路质量。
VALIDATION_R17.json为本地结果；REFERENCE_R16/R13文件仅是历史参考。
目标工具链编译链接、真实RS硬件CRC、物理线断开/恢复、完整加密归档和多设备物理总线均需继续实测。
未完成PL压缩、4G切换、OTA或预测准确率验收；本轮不改这些模块。
