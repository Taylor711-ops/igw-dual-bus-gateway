# R23：CAN／RS485 共享 SD 文件

适用项目：安路 DR1M90 工业边缘网关。主机 Linux 192.168.31.222，电脑 192.168.31.100，一块裸机从机提供逻辑设备1/2。

本版依据用户实际导出的 SDK 实现，FatFs R0.14b（FF_DEFINED86631），`FATFS_DRV_SD=1` 对应 MMC0。现有工程已包含 FatFs 头文件路径并启用 MMC 驱动。用户现用 main.c 与 R21.1 基线一致。详见 SDK_AUDIT.json。

## 已部署R23的网页归档修补（2026-10-28）

若日志显示R23版本却使用`/tmp/igw_r22_files.*`，原启动脚本会跳过保存，导致EVIDENCE_SAVED=0。此修正版已修复Web临时目录，并让启动脚本兼容两种目录。现有二进制不需要重编译；把本包`r23_archive_fix`文件夹复制到主机SD的`igw_apps/`，按其中README先补存原日志，再安装修补。

补存命令在主机Linux执行：`sh save_existing_evidence.sh /tmp/igw_r22_files.dSpmSX`。原目录仍需存在；主机重启后/tmp消失则不能凭本工具恢复。安装命令`sh apply_archive_fix.sh`会备份原脚本及清单，只更新归档脚本对应哈希。

修补不改变SD存储、CAN/RS485传输、从机应用或PL。保留旧文件的verify结果与归档是否保存是两项独立检查；未补存前不能把EVIDENCE_SAVED=0写成已经自动保存。

## 本版改变

- 新 CAN 分块协议与 RS485 FC42/43 文件协议访问同一组 SD 文件。
- 单文件上限 8388608 字节（8 MiB）；192字节应用块、CAN credit4 不变。
- 从机不再为每个文件分配整块 RAM。固定 SD 工作区和两个逻辑设备的协议状态，本地相同配置约17712字节；最终 ARM map 仍须由用户 SDK 验证。这不是整个程序BSS大小。
- 每个设备有 A/B 两代数据文件和各自的提交记录。新PUT仅重用非当前槽，新文件完整写入、同步关闭、重新读取并通过SHA256后才写提交记录并读回确认。
- 未完成PUT、超时、错误SHA或提交错误不会主动删除当前已提交版本。重新启动扫描两代记录与实际文件SHA，选择最高有效代；较新版本损坏时可回退到上一有效代。
- GET、GENERATE、网页上传后独立读回、跨总线取回保留。停止/失败后主机服务仍需检查并重启，不自动整文件重发。
- SD不可用时返回storage错误code9，不静默退回RAM。原R18的64KiB RAM兼容协议仍独立保留，本版文件服务使用新分块ID和SD银行。

本版增加容量和持久存储，不承诺比R21更快。8MiB上限保留了现有CAN报文预算的余量；不能仅改常量就宣称支持任意大文件。主机Linux工具仍对单文件使用内存缓冲，后续更大容量需要主机流式I/O及传输编号策略一起调整。

## 第一步：只更新从机应用

1. 备份从机工程当前 main.c，备份放在工程目录外，避免SDK递归编译到两个main。
2. 用本包 **slave_r23/slave/main.c** 替换 Windows SDK 从机工程中的 main.c。仅替换这一文件；不要把tests、vendor_ff或整个压缩包复制到SDK工程。
3. SDK执行 Clean → Rebuild。现有链接脚本有堆/栈保护，须编译链接成功再烧录。不要改RAM长度、栈地址、平台或主机BOOT.bin/DTB。
4. 从机断电时插好其独立 SD 卡，随后按原来的从机应用固化流程烧录并启动。卡需要现有 FAT32 或 FAT16 文件系统，本程序不格式化，也不支持当前SDK未启用的exFAT。
5. 首次运行只会在从机SD创建专用 `IGW23` 目录和所有权标记。现有非空目录没有有效标记时拒绝使用，不覆盖其中内容。

预期日志：

```text
[IGW-DUAL] gateway SD_FILE_R23 role=SLAVE ...
[SD-R23] MOUNT=PASS drive=1: directory=IGW23 FatFs=86631 max=8388608 last_fresult=0 reason=ready
[SD-R23] unit=1 valid=0 bytes=0 generation=0 slot=A
[SD-R23] unit=2 valid=0 bytes=0 generation=0 slot=A
[FILE-R23] ... storage=READY
```

首次卡上没有文件时valid=0正常。以后启动已提交文件会显示valid=1、原字节数和generation。

如果MOUNT=FAIL、storage=UNAVAILABLE、编译错误或停在MMC初始化：先发完整日志，不要继续大文件测试、格式化卡或反复生成新的BOOT。最后的legacy总线READY行不能代替SD挂载成功。

## 第二步：Ubuntu编译主机应用

在Ubuntu主目录解压本包后：

```sh
cd ~/IGW_R23_SD_FILES
bash build.sh
```

沿用已有交叉编译器：`~/uisrc-lab-anlogic/tools/aarch64-linux/bin/aarch64-linux-gnu-gcc`。可用IGW_AARCH64_CC指定其他实际路径。

将生成deploy内全部 **11个文件**复制到主机SD的 `igw_apps/r23_sd_files/`：

- igw_can_block、igw_rs485_file、igw_file_web
- run_file_web.sh、run_sd_test.sh
- input_4096.bin、input_65536.bin、input_1048576.bin、reverse_4096.bin
- build_info.txt、SHA256SUMS.txt

新主机需配套SD_FILE_R23从机。保留R22主机目录和原R21.1固件，用于回退。

## 第三步：先4KiB，再验证重启保存

从机启动看到SD READY后，在主机Linux串口执行：

```sh
/etc/init.d/S95igw_gateway stop
cd /mnt/mmcblk0p1/igw_apps/r23_sd_files
sha256sum -c SHA256SUMS.txt
sh run_sd_test.sh can 4096
```

这轮会写入设备1、读回比较，并让设备2生成另一份4KiB文件。通过后：

```sh
sh run_sd_test.sh cross 4096
```

通过后保持主机服务/测试停止，等所有传输已结束，给 **从机板** 断电再上电，SD卡留在从机。等待SD挂载成功。主机执行：

```sh
sh run_sd_test.sh verify 4096
```

verify只通过CAN和RS485取回设备1，不PUT也不GENERATE，再与主机原input_4096.bin比较。PASS才说明这一轮实际掉电重启保存通过。对应SHA256：

```text
6623b07137c4df118992dad67a06d7f99a8e41914781595d84c191dae9fe9c25
```

CLI证据自动保存到主机SD：`/mnt/mmcblk0p1/igw_evidence/r23_sd_files/igw_r23_file.*`。

## 第四步：网页与较大容量

先完成4KiB和verify，再启动网页：

```sh
sh run_file_web.sh
```

电脑打开 http://192.168.31.222:8081 ，Ctrl+F5刷新。标题应显示R23 / SD_FILE_R23，单文件上限8MiB。操作习惯与R22一致。

建议先做1MiB CAN下发、RS485取回及电脑端SHA256一致，再做“已有文件保持”测试：先成功写入4KiB基线，再开始下发1MiB并在进度增长后断开相应总线；等待失败并退出保存证据。接好总线、重启主机服务后，直接GET应取回之前的4KiB基线。不要重新PUT或GENERATE覆盖它。

容量阶段可以执行：

```sh
sh run_sd_test.sh generate 8388608
```

这是设备2生成8MiB、提交到SD并经CAN取回一次，不是三次大文件测试。按R21基线估计约13分钟，SD写入及验证开销另计，实际结果为准。没有必要一开始就把8MiB完整矩阵跑完。

网页退出证据路径：`/mnt/mmcblk0p1/igw_evidence/r23_file_web/igw_r23_files.*`。主机归档与从机持久文件是两个不同位置。

## 从机SD文件说明

专用目录 `1:/IGW23`：

- OWNER.ID：目录用途标记。
- U01A.BIN / U01B.BIN：设备1的两代数据。
- U01A.MET / U01B.MET：设备1的对应提交记录。
- U02A/B的BIN、MET：设备2。

每份记录包含magic、设备号、长度、64位generation、数据SHA256以及记录本身的SHA256。目录只包含已提交记录不等于数据一定完好，启动会重新检查实际文件长度和SHA。不要手动修改/重命名这些文件来选择活动版本。

每个逻辑设备最多占两份8MiB数据，两个设备合计约32MiB再加文件系统开销。工作区不随文件大小增长。

## 已做验证与实际边界

本地使用**用户提供的真实FatFs源码/配置**，搭配RAM磁盘/映像适配器；不是将FatFs API全部替换为成功返回值。验证覆盖8MiB流式生成与逐块读回、跨进程重新加载、A/B版本选择、未完成PUT、错误digest、提交写错误、较新数据/记录损坏回退、非空无标记目录拒绝。

另有实际Linux C客户端与CAN/RTU协议的管道测试、实际生成的从机调度器测试、网页异步任务和停止清理测试。编译时检查了用户真实SDK头文件；未在本环境执行ARM交叉链接，也没有访问实物SD卡。最终结果见VALIDATION_R23.json与LOCAL_CHECKS.log。

需要明确的限制：

- 物理SD挂载、实际掉电重启、实际8MiB总线传输仍待用户板上验证。
- 双代方案提供应用层提交选择及已观察故障路径的旧文件保留；FAT本身和SD卡内部缓存不具备本程序可证明的任意断电瞬间原子性。SDK的CTRL_SYNC只是返回成功，没有额外卡缓存屏障，不能据此宣称所有掉电故障均可恢复。
- SDK的MMC HAL读写Timeout参数实际未使用；设备层部分操作有轮询超时，初始化中仍存在无上限循环。本版没有改写SDK底层驱动，不宣称支持运行中拔SD卡。先验证正常卡的挂载、读写，再做受控的已提交后重启。
- 当前恢复仍从头传输，未实现持久断点续传；文件仍为明文，独立文件服务不启动NPU/原AES归档监控。
- 物理多从机、PL压缩、以太网服务器高速文件交换等高级项目另行验证，不能由本版本推定完成。
