# R13 从机应用

使用已有 RW_R9 的裸机工程、HPF、SDK、链接脚本和 PL。实际替换的文件为本目录 `slave/main.c`。默认一个物理从机服务两个独立逻辑节点，CAN 和 RS485 的同地址共享一组寄存器，不同地址互不共享。

节点 1：CAN 0x211/0x191，Modbus unit 1。
节点 2：CAN 0x212/0x192，Modbus unit 2。
对应一般规则为 request=0x210+unit、reply=0x190+unit。R0=0x1234、R1=各自地址、R4=0x0801、R8=0x0901；R9=0x0d01 表示本扩展，R10=本固件节点数；R16..31 为易失测试寄存器。

**不要把从机 0x80100000 / 100MHz 的 RS485 配置复制给主机。** 主机仍为 0x80400000 / 50MHz。节点数改变不改变波特率。

源码维护入口为 slave_src，修改后 `python3 generate_slave.py` 重建 main.c。自定义例子：

```sh
# 一块板模拟 16 个逻辑节点，不能写成 16 台物理设备实测
python3 generate_slave.py --first-node 1 --nodes 16
# 真实多从机之一：此板仅应答设备 2
python3 generate_slave.py --first-node 2 --nodes 1
```

自定义生成会改变本包校验过的 main.c；其编译/实测必须单独记录。主机预先编译的程序通过 IGW_NODES 配置轮询数量，默认演示保持 2。主机总线连接两个逻辑节点只需既有一组 CAN 线和一组 RS485 线，不为虚拟节点增加接线。

已有真实 SDK 可运行 `python3 check_slave_sdk.py --sdk /path/to/igw_can_plat` 做语法检查。它不是 ARM 编译链接，更不是 QSPI 烧写结果。默认两节点生成文件已完成真实 SDK 语法检查和带 ASan/UBSan 的调度器检查；真实硬件待测。
