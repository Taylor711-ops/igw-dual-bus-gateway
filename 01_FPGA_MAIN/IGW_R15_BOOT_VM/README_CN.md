# R15 主机启动包：沿用 Ubuntu 虚拟机的厂商工具

本包用于生成 R15 Linux 主机的候选 `BOOT_R15.bin`。**不是从机工程，不需要重新导出 HPF，也不需要重编译 Linux、rootfs 或 U-Boot。**

## 直接操作

1. 把整个 `IGW_R15_BOOT_VM` 文件夹复制/解压到昨天的 Ubuntu 虚拟机主目录，确认存在：

   ```text
   /home/uisrc/IGW_R15_BOOT_VM/build_r15_boot.sh
   ```

2. 在该虚拟机执行：

   ```sh
   cd ~/IGW_R15_BOOT_VM
   bash build_r15_boot.sh
   ```

无需重新 `source drcfg.sh`。脚本直接使用已确认的 SDK 内厂商工具路径，不依赖反复配置 PATH。

3. 成功时末尾会显示：

   ```text
   RESULT=R15_BOOT_BUILT_OFFLINE_CHECKS_PASS
   UPLOAD_REVIEW_ARCHIVE=/home/uisrc/IGW_R15_BOOT_VM/build_r15.XXXXXXXX/IGW_R15_BOOT_REVIEW.tar.gz
   ```

4. 把这份 `IGW_R15_BOOT_REVIEW.tar.gz` 上传核对。此时先不要把候选镜像写到 SD；包内没有自动部署/烧写动作。若报错，保留并发送完整终端输出，以及提示目录中的 `albg.log`。

## 输入来自哪里

| 输入 | 来源及已完成检查 |
| --- | --- |
| `inputs/soc_fsbl.elf` | 你上传的虚拟机原 FSBL；ELF64 AArch64，入口/加载地址 `0x61000000`，初始化加载段 187264 字节，与原可启动 BOOT 的对应载荷逐字节一致 |
| `inputs/u-boot.bin` | 从你上传、SHA256 已固定的原 BOOT.bin 第三个分区原样提取，长度 926280 字节；没有重新编译或改写 U-Boot |
| `inputs/system_r15.bit` | 本次已经布局布线通过的 R15 bitstream，即上传的 `soc_prj(5).bit`；SHA256 已固定 |
| `inputs/system_r15_gateway.dtb` | 已完成主机板冷启动检查的精简设备树；本脚本不把它写回 SD |
| `reference/BOOT_R2_REFERENCE.bin` | 你提供的旧 BOOT，保留用于构建前后逐分区比对，不会被修改 |

R15 与输入工程的 PS IP 11 个文件已逐字节核对一致。沿用原 FSBL 的依据是 PS 配置保持一致且加载内容与现用镜像一致；不是任意同名 FSBL 都能混用。本包不采用从机 `igw_can_fsbl/igw_can_rs` 的打包配置。

## 使用的原厂流程

已检查你虚拟机上传的 `create_dr1m_image.sh`，实际调用的是：

```text
~/uisrc-lab-anlogic/tools/bootgen/linux64/bin/albg
```

脚本使用同一厂商工具和原 SDK 非 QT 分支的 BIF 参数，只在本包新建的目录构建：

```text
FSBL: bootloader, destination_cpu=apu-0, checksum=sha2
PL:   destination_device=pl, partition_owner=fsbl, encryption=none,
      checksum=sha2, load=0x200000
U-Boot: destination_device=ps, destination_cpu=apu-0, org_endian,
        checksum=sha2, load=0x200000, exec=0x200000, partition_owner=fsbl
```

这些加载/执行地址及分区属性同时与旧 BOOT 的分区头匹配。镜像头、分区头和厂商校验数据由 **albg** 生成，本包的 Python 工具只检查结果，不生成或修补 BOOT 容器。

如果 SDK 目录不是默认路径，可在运行脚本时明确指定现有 SDK，例如：

```sh
IGW_SDK_DIR=/实际的SDK目录 bash build_r15_boot.sh
```

本次已经确认的默认 SDK 链接 `~/uisrc-lab-anlogic` 即可使用，不需要改动。

## 构建后自动检查

- 所有交付文件的 SHA256，以及启动输入的固定 SHA256。
- 原镜像与新镜像的启动头/三个分区头 CRC32、分区边界与非重叠关系。
- 新镜像 FSBL 与 U-Boot 的初始化载荷与原镜像逐字节一致。
- 三个分区的加载地址、执行地址和属性与原镜像一致。
- 新 PL 分区与本次 R15 `.bit` 的二进制内容匹配，检查厂商容器中的 32 位字节序转换。

厂商 `sha2` 校验由原厂打包工具生成；本包的只读检查器没有独立复现该厂商校验数据编码，因此报告中 `vendor_sha2_digest_independently_verified` 明确为 `false`。不能把这些离线检查当成板上启动、推理或 CAN 验收。

## 保留原文件

脚本不会修改：

- SDK 的 `boards/files`、`boards/DR1M/buildroot/output`、`images` 或配置文件。
- SD 卡、原 BOOT、从机固件、Linux 内核和 initrd。
- 已经完成冷启动检查的主机设备树。

每次运行单独创建 `build_r15.XXXXXXXX` 目录，不覆盖旧构建。候选镜像名字为 `BOOT_R15.bin`，不会自动替换 `BOOT.bin`。

## 上板前后的顺序

1. 上传构建结果归档，核对厂商日志和新镜像。
2. 再把新镜像与当前精简 `system.dtb` 配套，备份旧镜像后进行主机冷启动。
3. 先核对新镜像身份、CAN 被动解析窗口和 RS485，再运行 CAN/RS485/NPU/Web/加密归档联合回归。

原设备树启动测试中 NPU capability 为 `0xBF`、网卡为 `192.168.31.222`，只证明旧硬件与精简 Linux 配置的启动检查通过。R15 新硬件还没有上板，不能提前宣称中级任务整体验收完成。
