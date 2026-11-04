# R17.1：主动上报与原Web计数校验兼容修复（仅主机）

R17板上已经证明CAN/RS485自主上报、普通读写可以并行运行。
2026-10-26的完整联调JSON同时暴露了一个软件兼容缺陷：旧Web要求每次普通Modbus应答之间PL GOOD只增加1；R17合法上传也增加GOOD，因此部分CROSS在写入前被拦截，NPU采样窗口不断重置。
用户证据中CROSS为5次PASS/2次REVIEW；在线NPU total=0、rejected=48、HARD=450（仅启动自检）。不能把那轮记为完整系统通过。

本版修复该问题，不更改从机、位流、BOOT、设备树、模型、特征计算或AES实现。
本包为源码，本地验证通过后仍需要目标板回归；不含目标ELF。

## 修复方式

RS485唯一所有者对每一帧实际通过时隙、会话、序号、CRC和数据验证的上传计数；包括真实重复帧，但不把错误帧计入合法上传。
普通UDP应答增加pl_accounting，给出随机owner_session和validated_uplink_good，以及上传错误累计值。
Web严格要求：

    本次PL GOOD增量 = 1个普通应答 + 已验证上传GOOD增量
    本次PL TX增量 = 1个普通请求

仍拒绝不能解释的额外帧、额外TX、计数重置、所有者变更、CRC/UART/gap/overrun/watchdog新增和上传错误新增。
32位PL计数回绕有明确检查；没有把GOOD检查改成宽松的大于等于。
缺少扩展字段的旧网关继续按原严格规则处理；同一会话不能无提示降级。
仅修复NPU输入采样被错误拒绝的问题，NPU模型及准确率边界不变。

## 1. Ubuntu虚拟机编译

将整个目录解压到用户主目录：

```sh
cd ~/IGW_R17_1_ACCOUNTING_FIX_SOURCE
bash build.sh
```

沿用原交叉工具链。看到BUILD_COMPLETE_R17_1后，将整个deploy文件夹复制回Windows。
**从机保持已经烧录的UPLINK_R17，不重新烧录。主从硬件与主机BOOT/system.dtb继续沿用。**

## 2. 用SD卡安装主机新目录

先按Ctrl+C正常停止旧服务，保留本轮加密文件igw-c94a32bd2e996c85f2615438.igwe和导出JSON。
主机执行sync、poweroff，关机断电后取SD卡。
把deploy内全部文件复制到SD卡的：

    igw_apps/r17_1_rs485_uplink_install/

原igw_apps/r17_rs485_uplink必须保留，安装器会校验并从中复制已有CAN、vault、NPU和采集程序。
本版另建r17_1_rs485_uplink，不覆盖旧R17。
上电登录后逐条执行：

```sh
/etc/init.d/S95igw_gateway stop
cd /mnt/mmcblk0p1/igw_apps/r17_1_rs485_uplink_install
sha256sum -c SHA256SUMS.txt
sh install_on_board.sh
```

看到R17_1_STAGED=/mnt/mmcblk0p1/igw_apps/r17_1_rs485_uplink。
如果任何校验或安装失败，停止，不继续启动。不要删除活跃RS所有权锁。

## 3. 完整并发回归

```sh
cd /mnt/mmcblk0p1/igw_apps/r17_1_rs485_uplink
sh run_secure_gateway.sh
```

只在提示时输入私人归档口令。PC打开 http://192.168.31.222:8080 并Ctrl+F5。
页面标记R17.1，两个逻辑设备的CAN与RS485上报计数都应增长。
每个设备在地址16执行两次“双向交叉验证”，每次等待PASS再点下一次，不连续多次盲点。
完整运行至少90秒（最多600秒），确认NPU在线total>0、history有backend=Alnpu的结果；只有启动self-test=PASS或硬件HARD=450不够。
NPU rejected在稳定正常窗口应保持不增长；推理数会按5轮窗口逐步增长，不要求每3秒推理一次。
先导出JSON，再按Ctrl+C；记录最后一条ARCHIVE_SAVED的具体文件名。
停止后收集：

```sh
tail -8 /tmp/igw_web_rs485.log
tail -5 /tmp/igw_web_can.log
tail -5 /tmp/igw_r16_uplink.log
cat /tmp/igw_r15_plcan.stats
ip -details -statistics link show can0
devmem 0x8040000C 32
devmem 0x1f0000020 32
```

期望CROSS全部PASS、普通事务无新增错误、两种上传持续、PL新增错误为0、退出TX授权0/NPU能力BF。
rs485_reply的pl_accounting.schema应为IGW_RS485_PL_ACCOUNTING_R17_1，validated_uplink_good应持续增加。
JSON软件版本为R17.1_RS485_UPLINK，上传schema保持R16/R17以兼容既有协议。
使用最后打印的具体加密文件做verify/open核验；不要按FAT时间猜最新文件。

## 板上能力边界

仍为两块板（一块主机、一块从机模拟两个逻辑节点）；未验证多个物理从机。
RS上报依赖CAN协调，RS值本身实际经过RS485；不声称标准Modbus普通从机能自主抢占总线。
数据为设备本地产生的测试计数；NPU只分析设备1的既有通信特征，不声称预测准确率完成。
PLC CAN仍是被动采集，PS CAN负责实际收发和ACK，未在本版更改职责。
历史PL初始丢帧和窗口内新增丢帧必须区分；已验证上传计数不能用来掩盖其他额外物理帧。
所有源码校验和本地测试见VALIDATION_R17_1.json；原R17测试记录仅作历史参考。

## 本地测试

```sh
python3 tests/run_r17_1_checks.py
```

覆盖真实C Web/网关与受控上传并发的12次CROSS、5轮NPU采样进展、未知额外GOOD拒绝。
NPU响应是独立CPU参考UDP替身，不是目标硬件准确率证据。
生产exchange函数另有ASan/UBSan测试：精确计数、额外GOOD/TX、所有者变化、元数据缺失/降级、计数重置、CRC/上传错误和32位回绕。
保留原R17协议、生成从机调度器、上传/Web/AES及启动清理回归。
