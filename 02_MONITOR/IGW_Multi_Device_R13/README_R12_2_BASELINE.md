# IGW 双总线读写 + NPU + 加密网页 R12.2

本包把冻结的 R6 NPU 模型接入已验证的 R11 网页采样与加密归档。两块板仍为 Linux 主机 192.168.31.222、RW_R9 从机；PC 为192.168.31.100。**这是源码升级包，需要在原 Ubuntu 工具链编译。原 R12 已完成三类板端会话；R12.2 的显示修正与停止修正已通过主机检查，仍需上板复验。**

原 R11 已有板端证据：CAN/RS485 双向交叉写读、AES-GCM 归档认证/篡改拒绝、正常重启后恢复。R12.2 保留同一 CAN/RS485/加密/NPU 推理核心；本次只增加过渡窗口呈现、输入截断显示，并合入 R12.1 重复 TERM 清理修正。SD BOOT、PL、时钟、从机固件不变。保留原 SD `igw_apps/secure_r11` 作为回退版本。

## 先明确模型范围

冻结模型为 16→8(ReLU)→3，隐藏输出零点0，严格 Alnpu 两层硬件 FC。它此前使用 PC 单次请求的特征；R12 使用板端完整寄存器周期的事务 RTT 峰值，统计域已改变，**不能沿用旧分类准确率**。网页标注“实验性预测 / 新采样域准确率待验证”，JSON 固定标记 `PENDING_BOARD_DOMAIN_TEST`。数值校验 PASS 代表硬件输出与冻结模型参考在误差预算内，并不代表故障判别正确。

已有 R12 三份原始证据与独立审核脚本见 evidence/board_r12、tools/audit_board_evidence.py 和 evidence/BOARD_FINDINGS.md。64 条保留窗口已重算特征、量化和数值参考；RS485 恢复混合窗口存在一次误报 CAN。R12.2 不更换或修正该原始标签，不改模型：只在同一5点窗口内某总线由可用变为不可用或反向时标记 window_phase=transition，网页主状态显示“切换中”，仍显示、归档原始预测。这个规则来自实际请求可用性，与模型预测独立；timeout 与 cooldown 均视为不可用，两者之间变化不算恢复。稳定窗口继续显示原始预测。它是展示层的暂不判定策略，并未修复或证明模型的过渡期准确率。

## 1. Ubuntu 编译

解压整个文件夹到 HOME，保留子目录，在 Ubuntu 运行：

```bash
cd "$HOME/IGW_NPU_Secure_R12_2" &&
bash build.sh &&
ls -lh deploy
```

默认工具链：`$HOME/uisrc-lab-anlogic/tools/aarch64-linux/bin/aarch64-linux-gnu-`。
默认 ArmNN SDK：`$HOME/uisrc-lab-anlogic/sources/buildroot/output/target/opt/armnn_lib`，必须含 `include/armnn/IRuntime.hpp` 和 `lib/libarmnn.so`。可用 `CROSS_COMPILE`、`MLK_BUILDROOT_DIR`、`IGW_ARMNN_ROOT` 指定已有安装；不要从其他版本拼接库。若提示缺文件，停在该步骤，提供具体报错。

build.sh 先核对源文件哈希，再编译 5 个 AArch64 ELF，并生成部署哈希。NPU 动态链接板上 ArmNN32.1，web/CAN/RS485 静态链接，vault 仍使用板上 libcrypto.so.1.1 的公开 EVP 接口。

编译后把 deploy **全部10个文件**复制到 Windows `C:\igw_npu_secure_r12_2`：

| 文件 | 用途 |
|---|---|
| igw_can_udp | 原 R9 CAN 引擎 |
| igw_rs485_rw | 原 R9 RS485 引擎 |
| igw_web | 采样、独立读回、NPU窗口及结果核对、HTTP、归档导出 |
| igw_vault | 原 R11 AES256-GCM 归档核心，源文件未改 |
| igw_fault_online | 适配为私有本机端口的 R6 严格 Alnpu 推理程序 |
| run_npu_local.sh | 自检450次 HARD IRQ 检查、锁与监督 |
| run_web_gateway.sh | 内部管理两条总线、NPU及网页 |
| run_secure_gateway.sh | 唯一启动入口；终端交互输入原口令 |
| build_info.txt | 编译信息和关键源文件哈希 |
| SHA256SUMS.txt | 部署文件哈希 |

## 2. Windows 下载服务

关闭旧网页服务的启动窗口（Ctrl+C），关闭旧 UDP 面板，保持从机 RW_R9 与接线正常。Windows CMD：

```bat
E:\python3.11\python.exe -m http.server 8018 --bind 0.0.0.0 --directory C:\igw_npu_secure_r12_2
```

## 3. Linux 主机下载，存入 SD 新目录

仅在主机 Buildroot 串口执行；不要输入从机周期打印串口。

```sh
mkdir -p /tmp/igw_npu_secure_r12_2 &&
cd /tmp/igw_npu_secure_r12_2 &&
wget -O igw_can_udp http://192.168.31.100:8018/igw_can_udp &&
wget -O igw_rs485_rw http://192.168.31.100:8018/igw_rs485_rw &&
wget -O igw_web http://192.168.31.100:8018/igw_web &&
wget -O igw_vault http://192.168.31.100:8018/igw_vault &&
wget -O igw_fault_online http://192.168.31.100:8018/igw_fault_online &&
wget -O run_npu_local.sh http://192.168.31.100:8018/run_npu_local.sh &&
wget -O run_web_gateway.sh http://192.168.31.100:8018/run_web_gateway.sh &&
wget -O run_secure_gateway.sh http://192.168.31.100:8018/run_secure_gateway.sh &&
wget -O build_info.txt http://192.168.31.100:8018/build_info.txt &&
wget -O SHA256SUMS.txt http://192.168.31.100:8018/SHA256SUMS.txt &&
sha256sum -c SHA256SUMS.txt &&
chmod +x igw_can_udp igw_rs485_rw igw_web igw_vault igw_fault_online &&
mkdir -p /mnt/mmcblk0p1/igw_apps/npu_secure_r12_2 &&
cp igw_can_udp igw_rs485_rw igw_web igw_vault igw_fault_online run_npu_local.sh run_web_gateway.sh run_secure_gateway.sh build_info.txt SHA256SUMS.txt /mnt/mmcblk0p1/igw_apps/npu_secure_r12_2/ &&
(cd /mnt/mmcblk0p1/igw_apps/npu_secure_r12_2 && sha256sum -c SHA256SUMS.txt) &&
sync
```

## 4. R12.2 上板复验

```sh
cd /mnt/mmcblk0p1/igw_apps/npu_secure_r12_2 &&
sh run_secure_gateway.sh
```

在本地串口输入 **R11 原归档口令**。程序复用 `/mnt/mmcblk0p1/igw_vault_r11/`，旧文件保留；不知道原口令时不要删除 vault 或重建。口令不要发到聊天或截图。

预计启动日志：

- 软件哈希全部 OK；NPU 启动数值自检 hidden150 + classifier150。
- `NPU_STARTUP_IRQ_DELTA HARD=450; self-test=PASS`。
- `NPU_LOCAL_READY UDP127.0.0.1:16002`。
- `R12 NPU + SECURE WEB READY: http://192.168.31.222:8080`。
- 第一个加密快照可在 NPU 预热完成前保存，此时 NPU 显示等待；这是有效的启动状态，不是推理通过。

PC 浏览器打开网页并强制刷新。先保持正常接线 **至少90秒**：NPU 每5个完整采样周期推理一次，约16秒产生一条，受实际采样与人工操作影响。确认后端 Alnpu、数值 PASS、结果数增长，再对一个易失寄存器执行一次“双向交叉验证”，导出会话 JSON。此步骤不会自动写寄存器。

不要把页面显示“正常”单独当作通过。验收至少核对：

1. 两总线有效且本窗口错误无新增；交叉写读 PASS、写计数+2。
2. NPU history 有实际窗口，包含16特征、5组源采样和精确 reply；实际 inference_count 增长。
3. 正常图每次推理为2个 HARD IRQ；总数含启动450个。NPU独占锁下核对增量，不能用进程存在代替推理证据。
4. `clipped_dims`、margin、CPU参考差值保留，不能隐藏异常或数值 REVIEW。
5. 串口至少出现后续 ARCHIVE_SAVED，停止后的最终档包含上述 NPU 和交叉记录。

保持串口启动窗口运行，另一主机串口/会话可检查：

```sh
cat /tmp/igw_npu_local_launcher.log
tail -12 /tmp/igw_npu_local_r12_*/program.log
grep -Ei 'npu|jpu' /proc/interrupts
ip -details -statistics link show can0
```

## 5. 停止并验证 NPU 已进入密文

原启动窗口 Ctrl+C。应显示 RS485授权0、NPU能力BF和 `SECURE_SERVICE_STOPPED`。记录 **最后一条 ARCHIVE_SAVED 的完整文件名**；板上时间未校准时不要用 `ls -t` 猜最新文件。

使用真实文件名替换下列 `igw-实际文件名.igwe`：

```sh
./igw_vault verify --input /mnt/mmcblk0p1/igw_vault_r11/igw-实际文件名.igwe
```

口令在串口本地输入。verify只在内存认证与篡改检查，不显示业务内容。如需核对 NPU 字段，可明确解密到易失 `/tmp` 新文件；不会覆盖已有文件：

```sh
./igw_vault open --input /mnt/mmcblk0p1/igw_vault_r11/igw-实际文件名.igwe --output /tmp/igw_r12_opened.json
cat /tmp/igw_r12_opened.json
```

检查 `software_revision=R12.2`、`presentation_policy=transition_abstention_v1`、`npu.history`非空、`reply.backend=Alnpu`、数值状态，以及交叉任务 PASS。导出的网页 JSON 和解密后的 `/tmp` 文件属于明文；只有 `.igwe` 是加密档。软件没有自动将 NPU 明文日志复制到 SD。

复验顺序：正常90秒→仅断 RS485约90秒（CAN保持连接）→恢复RS485约90秒。在混合窗口看到“切换中”，原始预测仍应可见；稳定恢复后看到正常。导出JSON，停止后记录最终密文名并认证打开，确认 window_phase 与原始 reply.label 同时保留。不要为了得到某个原始标签而反复切线。随后在同一次开机中重新启动、正常运行约30秒并停止，确认没有遗留 /tmp/igw_alnpu_probe.lock；watchdog 或硬件无响应情况下的保留锁规则不变。正式分类评估仍需记录人工故障时间和独立真实标签，按新契约计算混淆矩阵。标签只能用于评估，不能当作模型输入。实际类别不正确时，应报告失败并采集新域数据，不把网络丢包强行解释为断线。

## 行为与限制

- 原始总线读写只由原 web bus worker 发起。NPU 独立线程取已采集快照，无额外 CAN/RS485 发送，UDP16002仅绑定127.0.0.1。
- 推理服务自检和 HARD IRQ 检查通过后才开放。NPU异常时显示不可用，历史结果不冒充新结果；已经运行的通信与加密服务继续，NPU不自动重启。硬件启动/推理 watchdog 时保留独占锁，先停止服务并重启板，不手删锁再次入场。
- NPU窗口拒绝未知网络/协议错误，丢失采样代次或过长间隔时重新积累。仅正常、gateway_timeout和gateway_cooldown进入实验性分类。
- 最多32个推理窗口保存在内存。归档沿用R11约30秒间隔加密快照（实际间隔还包含加密耗时），正常会话被多次覆盖采样。断电前尚未完成保存的内存数据不能保证恢复。
- 加密格式仍为IGWVLT11，PBKDF2-SHA256600000、随机salt/nonce、AES256-GCM和认证header；R11旧档及密码兼容。只验证过正常重启，没有承诺写入中突然断电保护。
- 当前一个从机/逻辑节点。HTTP为受限局域网明文访问，没有TLS、用户角色或互联网安全承诺。
- 完整FPGA时序签核、多设备数量要求、真实业务故障数据准确率、长时间性能分位数及最终评分仍未完成；本包不能宣称中级目标全部达标。

## 主机软件检查

`python3 tests/run_checks.py` 使用 GCC/G++、Node（可选）、Python cryptography；只用于开发机。它运行生产 C 服务与未改 R9引擎的软件总线代理、独立 Python NPU数值代理、加密互通及监督脚本测试。`IGW_WEB_SANITIZE=1 ASAN_OPTIONS=detect_leaks=0 python3 tests/run_checks.py` 开启ASan/UBSan。

这些代理测试不能代替板端 NPU、SD、CAN/RS485 或实际准确性验收。真实 SDK 头文件语法检查已通过，本包实际 AArch64链接与 R12.2 联合上板需要在用户工具链完成。已有 R12 板端结果不能自动算作 R12.2 已验收。验证范围与证据见 VALIDATION.json、evidence 和 FEATURE_CONTRACT.json。
