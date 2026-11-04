# R17.2 从机串口打印对照版本

本包是针对正常运行时 RTT 峰值的从机应用对照版本，不是文件传输版本。
主机继续使用已安装的 R17.1；不替换主机 BOOT.bin、system.dtb、Linux 程序或任何 FPGA 位流。

## 已知现象与假设

2026-10-26 正常运行完整日志：CAN 9480/9480、RS485 1196/1196 成功，两次 CROSS 均通过。
CAN 中位 RTT 516 us、P99 604 us、最大 69867 us，27 次超过 50 ms；RS485 同样有 27 次超过 50 ms，最大 105525 us。
CAN 峰值全部出现在设备 1。原从机每 5 秒在通信主循环连续打印统计。
这些观察支持检查 UART 同步打印阻塞，但尚未证明因果；必须做板上 A/B 对照。
RTT 是应用往返耗时，不是单向转发时延，不能把关闭打印等同于量化评分达标。

## 修改范围

- 默认 IGW_RUNTIME_LOGGING=0：运行期间不批量打印统计。
- 启动、版本和初始化错误信息保留。
- 每 5 秒继续执行 Snapshot(AL_FALSE)，保留 CAN 错误计数及最大值采集。
- CAN/RS485 收发、共享寄存器、主动上传、时隙协调、超时及重传算法不变。
- 协议头和驱动源码与 R17 基线逐字节核对一致，见 CHANGE_AUDIT.json。
- 如需诊断，可在 SDK 编译宏中设 IGW_RUNTIME_LOGGING=1 恢复周期打印；该选项不用于本轮默认对照。

## 第一步：仅更新从机应用

1. 主机服务如仍运行，先用 Ctrl+C 正常停止并保存上一轮证据。
2. 备份当前从机 Vitis 应用的 main.c 和原可启动镜像，作为回退版本。
3. 用本包 slave/main.c 替换从机裸机应用中原来的 main.c。文件已合并协议和驱动实现；无需把 slave_src 单独加入编译。
4. 沿用当前从机 SDK、平台、链接脚本和原来的编译/下载流程。不要替换位流、HPF、FSBL 或主机 SD 启动文件。
5. 编译成功后按原方式运行/烧录从机应用，确认启动输出：

   [IGW-DUAL] gateway UPLINK_R17_2 role=SLAVE ...
   [DUAL-SLAVE] R17.2 READY ...
   [RUN-LOG] R17.2 periodic_uart=0; wire_protocol=R17; startup logs retained

之后不再每 5 秒输出 CLEAN/统计行是本轮预期。是否正常以主机网页、请求及上传日志为准。
本包仅完成本地主机模拟回归，真实 ARM SDK 编译、链接和板上延迟改善尚未验证。

## 第二步：主机保持 R17.1，正常接线对照 4 分钟

主从 CAN、RS485 保持连接，本轮不注入断线，不改串口或总线速率。
主机登录后逐行执行：

    mkdir -p /mnt/mmcblk0p1/igw_evidence/r17_2_quiet_4min
    cd /mnt/mmcblk0p1/igw_apps/r17_1_rs485_uplink
    sh run_secure_gateway.sh

口令仅在主机终端输入。READY 后网页 Ctrl+F5，从 READY 计时：
前 3 分钟只观察；第 3 分钟对设备 1、2 各执行一次 CROSS；第 4 分钟先导出会话 JSON，再 Ctrl+C。
此前峰值在约 188 秒开始出现，因此本次至少覆盖 4 分钟，不能只测 30 秒。
主机软件 revision 仍显示 R17.1_RS485_UPLINK；用从机启动行单独标识 R17.2。

停机后保存证据：

    cp /tmp/igw_web_can.log /tmp/igw_web_rs485.log /tmp/igw_web_service.log /tmp/igw_r16_uplink.log /tmp/igw_r17_rs485_uplink.json /tmp/igw_r15_plcan.stats /mnt/mmcblk0p1/igw_evidence/r17_2_quiet_4min/
    ip -details -statistics link show can0 > /mnt/mmcblk0p1/igw_evidence/r17_2_quiet_4min/can_after.txt
    sync

提交本轮 JSON、完整 CAN/RS485 日志、从机启动行和主机停机汇总。
比较完整 RTT 分布和峰值、错误增量、主动上传、CROSS、NPU 裁剪；不能只挑最后几条 RTT。
只有目标板对照支持时才能把阻塞打印确认为主要原因。如果峰值继续出现，保留现场并继续分析。

## 本地验证与重生成

    python3 run_checks.py

测试调用实际生成的从机调度函数，覆盖 CAN/RS485 服务、共享寄存器读写、两逻辑节点主动上传、丢 ACK/取消/释放。
对 quiet=0/诊断=1 两个构建分别运行，检查运行期日志输出、忙状态保护和 CAN 错误采样。
AddressSanitizer/UBSan 启用；受当前 ptrace 环境限制 LeakSanitizer 显式关闭，见 VALIDATION.json。
模拟测试不能替代板上 UART 时延、ARM 构建或实际文件传输测试。

编辑 slave_src 后运行 python3 generate_slave.py，再编译 slave/main.c。
SHA256SUMS.txt 为交付文件校验；run_checks.py 会重写同内容的生成文件和本地验证报告。

## 后续开发

本版不包含文件分片/重组。下一版本新增 CAN 双向文件传输，先验证小文件及完整 SHA-256，再接服务器/网页接口和 RS485。
原 R17 从机和主机 R17.1 是可回退基线，不能用此对照结果替代中级全部指标验收。
