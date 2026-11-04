# IGW R13：多设备寻址、双向读写、NPU 和加密网页

这是在已实测 R12.2 上建立的**独立源码候选版本**，尚未在两块 FPGA 板上部署验证。R12.2 回退目录不改。先完成本版本的多设备上板验收，再推进 PL CAN 解析和选定高级功能。当前不能宣布中级全部完成。

## 本次可用内容

- 主机 CAN 和 RS485 网关支持明确配置的节点 1–N，N=1–16；默认单独运行网关仍为 1，R13 总启动器默认 2。
- CAN 请求 ID=0x210+节点，应答 ID=0x190+节点；设备 1 保留原 0x211/0x191，旧 0x201/0x181 只服务节点 1。标准经典 CAN，500 kbit/s；不是 CANopen。
- RS485 使用节点地址 1–16，9600 8E1，FC03/FC06。PL 继续执行 UART、RTU 分帧和 CRC；当前软件实现选择最多 16 个地址，不宣称已实现全部 Modbus 设备功能。
- 各节点独立寄存器、写次数、最近写记录、CAN 写去重缓存；相同节点的 CAN/RS485 共用同一寄存器银行。寄存器 16–31 易失，不控制真实执行器。
- 每条总线只有一个事务在途，节点顺序轮询；CAN 与 RS485 并行采样。连接多个节点不意味着让多台 RS485 从机同时抢占发送。
- 网页有设备选择，分别显示各设备寄存器/可用性；导出和加密快照带完整 devices 数组、每笔操作 unit 和原始帧。计数明确为 shared_bus 总线累计数，不能相加当成逐设备成功数。
- 写命令只发送一次；不明确的写应答保留 unknown；HTTP 操作 ID 重复不会再执行，换设备复用同 ID 会被拒绝。网关 UDP 去重按节点隔离。
- 冻结 NPU 权重不变，**仅节点 1** 的五次配对采样窗口进入模型。其他设备不混入特征。多节点负载改变采样时序，R13 数值/分类仍需目标板复验。当前是通信异常诊断，未实现未来设备故障预测。
- `pc/start_accept_2nodes.cmd` 自动执行两个节点交叉读写和银行隔离验证，导出 JSON；独立 `tools/audit_multi.py` 检查原始 CAN 帧、Modbus CRC 和读回链路。

只有一块从机时，默认 R13 从机在同一 FPGA 上模拟两个独立节点。必须在报告中写“2 个逻辑节点、1 台物理从机”；不能写“16 台工业设备实测”。另一种固件配置是每块物理从机只服务一个不同地址，接口支持它，但本次没有这样的多物理从机验收证据。

## 1. Ubuntu 编译主机程序

把本目录完整放入 Ubuntu HOME，然后执行：

```sh
cd ~/IGW_Multi_Device_R13
bash build.sh
```

沿用以前成功编译 R12.2 的 AArch64 Linux 工具链和 ArmNN SDK。默认查找 `~/uisrc-lab-anlogic/tools/aarch64-linux/bin/aarch64-linux-gnu-` 和 Buildroot target 的 ArmNN；自定义位置沿用 CROSS_COMPILE / MLK_BUILDROOT_DIR / IGW_ARMNN_ROOT。

成功后 deploy 中共 **11 个文件**：5 个程序、3 个启动脚本、install_r13.sh、build_info.txt、SHA256SUMS.txt。把**整个 deploy 文件夹**复制回 Windows 本 R13 目录，覆盖该副本的 deploy 文件夹。不要只复制单个程序，也不要混入旧版本程序。源码包本身不含伪装成已编译结果的 ELF 或 BOOT.bin。

## 2. 从机应用升级

将 `slave_r13/slave/main.c` 替换到此前成功运行 RW_R9 的**从机裸机应用工程 main.c**，使用原有从机 SDK/链接脚本编译，沿用原有下载或 QSPI 应用更新流程。

这是**从机应用源码**，不放进 Linux 主机的 deploy 目录，不覆盖主机 BOOT.bin，不将主机的 R2 FPGA bitstream 刷到从机。生成文件已配置 FIRST_NODE=1、NODE_COUNT=2。完整说明见 `slave_r13/README.md`。

启动日志应出现：

```text
[IGW-DUAL] gateway MULTI_R13 role=SLAVE ...
[DUAL-SLAVE] R13 READY first_node=1 nodes=2; independent banks; ...
```

R9 日志不能证明 R13 多节点已就绪。当前没有本环境的目标板交叉编译器或 IDE，已做真实 SDK 头文件语法检查；ARM 编译链接和目标板运行仍待用户端完成。

## 3. 安装 Linux 主机候选版本

Windows 以太网保持 192.168.31.100。双击本目录 `serve_deploy.cmd`，它在 8019 端口提供已编译的 deploy。先用主机串口 Ctrl+C 停止旧启动器，等待 SECURE_SERVICE_STOPPED 和 `#`；从机保持运行。

在 **Linux 主机板**执行：

```sh
wget -O /tmp/install_r13.sh http://192.168.31.100:8019/install_r13.sh
sh /tmp/install_r13.sh
```

安装器只把完整校验后的版本保存到 `/mnt/mmcblk0p1/igw_apps/multi_r13`，不替换 R12.2，不启动服务，不改 BOOT。已有 R13 目录时只校验，不自动覆盖。

从机 R13 就绪后，在同一主机串口运行：

```sh
cd /mnt/mmcblk0p1/igw_apps/multi_r13
IGW_NODES=2 sh run_secure_gateway.sh
```

在口令提示处输入原 vault 的完整私人口令，不发送到聊天。继续使用既有 `/mnt/mmcblk0p1/igw_vault_r11`，格式和密钥机制不变。等待 `R13 MULTI-DEVICE + NPU + SECURE WEB READY`，电脑打开 http://192.168.31.222:8080，设备选择应有设备 1 和设备 2。

## 4. 自动验收

保留主机和从机运行。关闭旧 UDP 面板，不在浏览器手动写入。Windows 双击 `pc/start_accept_2nodes.cmd`。

它会对每个节点在 R31 做 CAN 写→RS485 读、RS485 写→CAN 读，再分别写入 R16：设备 1=1111、设备 2=2222，然后经两条总线分别读回。写入仅发生在这些易失测试寄存器；任何写请求都不自动重试。

成功输出 `PASS_LOGICAL_MULTI_NODE_EVIDENCE`，证据在 pc 下新建的时间戳文件夹，含 evidence.json、events.json、report.json、SHA256SUMS.txt。**把整个文件夹压缩发回来**，不只发成功截图。若 REVIEW，保留 pending_command 操作 ID；先核对结果，不能把原写请求直接再发一遍。

也可在 Ubuntu/Windows Python 3 中复核：

```sh
python3 tools/audit_multi.py /path/to/evidence.json --nodes 2
```

审计结论只覆盖逻辑节点、帧绑定、双向读回、银行隔离；不认证物理设备数量、NPU 分类准确率、转发时延或 PL 时序。

## 5. 停止、归档、回退

主机串口 Ctrl+C，等待最终 ARCHIVE_SAVED 和 SECURE_SERVICE_STOPPED。核对 `devmem 0x8040000C 32`=0、`devmem 0x1f0000020 32`=0xBF；使用原口令 `igw_vault verify --input ...` 校验最终密文。

回退时停止 R13，从 SD 原 `npu_secure_r12_2` 目录启动旧脚本；需要完整冻结基线时同时恢复原 RW_R9 从机应用。R13 也能以 `IGW_NODES=1` 对单节点运行兼容测试，但不能把这称作完整 R12.2 回归证据。主机 BOOT 保持已固定 SHA256：7b81535086c3096ded1e15061118236978730d56f9132d2616bc573cc8dd734c。

## 验证与剩余事项

实际执行记录见 VALIDATION.json 和 evidence/r13_host。已验证的 R12.2 板端历史放在冻结基线包中，不转记为 R13 板端通过。本包 baseline_r9 和 baseline_stop_r12 为历史回归夹具，不用于本次部署。

下一步及中高级边界见 PROJECT_STATUS.md。不要重命名 PASS 字样来代替实测结果。
