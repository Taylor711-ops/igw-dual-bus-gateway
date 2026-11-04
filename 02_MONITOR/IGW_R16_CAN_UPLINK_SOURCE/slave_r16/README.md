# R16 从机应用源码

把 slave/main.c 替换到当前成功运行 R13 的从机裸机应用工程 main.c，使用原从机 SDK/链接脚本编译并按原流程更新应用（或重新打包从机自己的 QSPI 镜像）。
保留从机原 FPGA 位流、FSBL 和硬件平台；不要刷入 Linux 主机的 R15 BOOT.bin。

默认服务 unit=1、2 两个独立逻辑节点。启动标识 UPLINK_R16 / R16 READY。
保留原 CAN/RS485 请求读写，新增自主 CAN HELLO/DATA 上传与应用确认；源数据是设备本地产生的每秒测试计数，显示在设备 R11。
主机新接收区 R16 与从机共享寄存器 R16 不同；旧 CROSS 操作仍用原数据区。

主机接收器不在线时重试有上限，不会等待而阻塞原双总线。RS485 主动上传未实现。

生成：python3 generate_slave.py
使用实际 SDK 头文件做语法检查：python3 check_slave_sdk.py --sdk 原igw_can_plat目录
本包没有目标 ELF；目标 SDK 编译/链接及两板运行待用户执行。
完整操作见上一层 README.md。REFERENCE_R13_* 仅为继承记录。
