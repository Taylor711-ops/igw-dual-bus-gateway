# R15 启动设备树修复 FIX1

适用主机 BOOT.bin SHA256：
83a016557c5e93f9bf26ef5017fce67eb4221a28322c24bdf8b19c81e8075215

启动截图出现 Kernel panic / Asynchronous SError Interrupt，调用栈含 anlogic_dynclk_driver_init。
前版设备树遗漏 /soc/axi_dynclk@80101000。R15 已移除显示动态时钟硬件，但 Linux 仍会初始化它，访问已移除的 MMIO 可引发总线异常。这是本次优先修复点；实际冷启动仍需验证。

本包 system.dtb 在前版 R15 设备树基础上只增加该节点 status="disabled"。
相对原始厂商设备树共禁用四个节点：vdmafb@80100000、axi_dynclk@80101000、vdma@80200000、mipicam@80300000。
保留全部 99 个节点及其他属性，PS CAN、网络、串口、SD、NPU 配置不变。

## 替换步骤

1. 当前主机若停在 Kernel panic，没有可用 shell，直接关闭主机电源，取出主机 SD 卡。
2. 将现有 system.dtb 备份到电脑。解压本包，把本目录的 system.dtb 复制到 SD 卡根目录覆盖同名文件。
3. 保留刚才哈希匹配的 R15 BOOT.bin；不重新打包 BOOT，不重新编译位流，不修改从机。
4. Windows 命令提示符执行（F: 请按实际 SD 盘符替换）：

```bat
certutil -hashfile F:\system.dtb SHA256
```

预期 SHA256：
771d4692c3de93c00095515359f769becf2cba8aee144752905dec984745a1af

5. 安全弹出，插回主机 SD 卡，重新上电，保存从上电开始的串口日志。

## 登录后的第一组检查

先停止原 CAN 自动服务，再检查身份；不要启动业务程序：

```sh
/etc/init.d/S95igw_gateway stop
for n in vdmafb@80100000 axi_dynclk@80101000 vdma@80200000 mipicam@80300000; do
    echo "$n"
    tr -d '\000' < "/sys/firmware/devicetree/base/soc/$n/status"
    echo
done
dmesg | grep -Ei 'dynclk|vdmafb|anlogic-dma|mipicam|hard_npu|soft_npu'
devmem 0x80400000 32
devmem 0x80400800 32
devmem 0x1f0000020 32
ip -4 addr show dev eth0
```

四个 status 应均为 disabled。三个寄存器预期依次为 0x32574749、0x4E414349、0x000000BF。
不要用 devmem 访问 0x80101000 或其他移除的视频 MMIO；本次修复目的是停止访问不存在的硬件。
NPU 能力值只证明静态寄存器可访问，不证明推理已通过。

若仍然崩溃，请保留完整启动日志，尤其是第一次 SError 前的信息。不要反复替换其他文件。

## 本地验证

已通过 libfdt 完整结构检查、DTB/DTS 往返语义检查、与原始设备树全属性比较。
全树只有上述四处 status 改动；相对前版 R15 仅增加 dynclk 禁用。
四个 0x80000000..0x8fffffff 区间的顶层设备树 MMIO 节点均已禁用。
dtc 原版和修复版各 34 条原有警告；尚未完成修复版板上冷启动。

tools/verify_stage.py 可用 Python 3 重新核对语义和原始 boot.scr CRC。
reference 为审计参考，不要把其中旧设备树覆盖回本次 system.dtb。
