# 智联双总线工业协议融合网关系统 技术代码

本压缩包按主机 FPGA、监控与设备、文件传输三个部分整理项目源码、工程配置和构建脚本，供比赛评审及后续维护使用。各目录保留原发布包的相对结构、原始说明和校验文件。它们是按既有版本逐步叠加的工程，不是将所有源文件直接放在一起编译的单一工程。

整理日期：2026 年 11 月 4 日。

## 一、系统与代码对应关系

| 部分 | 设备或功能 | 主要入口 |
| --- | --- | --- |
| 安路主机 PL | RS485 硬件收发、帧处理、AXI 接口；CAN 帧旁路观察 | `01_FPGA_MAIN/IGW_GATEWAY_R15_CORE/project_r15/soc_prj/soc_prj.al` |
| 安路主机 PS | Linux 下 CAN/RS485 通信、调度、网页监控、归档与 NPU 调用 | `02_MONITOR/IGW_R26_BIDIRECTIONAL_DEMO/src/`，设备 3 上报扩展见 `IGW_ZYNQ_RS4853_UPLINK/` |
| 安路从机 | 一块物理从机板，提供支持 CAN 和 RS485 的逻辑设备 1、2 | `02_MONITOR/IGW_R26_BIDIRECTIONAL_DEMO/slave_r26/slave/main.c` 和 `slave_r26/slave_src/` |
| Zynq 设备 3 | 通过 RS485 实现数值读写、受控上报及 TF 卡文件收发 | `03_FILE_TRANSFER/IGW_ZYNQ_RS4853_FILE/zynq/` |
| 监控网页 | 设备选择、读写、上报、状态展示、NPU 结果和证据导出 | `02_MONITOR/IGW_ZYNQ_RS4853_UPLINK/host/monitor/web/index.html` 和 `host/monitor/src/` |
| 文件网页与传输 | 文件下发、设备生成、取回、独立读回和 SHA-256 校验 | `03_FILE_TRANSFER/IGW_CAN1_RS3_PARALLEL/host/file_page.html` 和 `host/src/` |

当前系统对应一块安路主机、两块物理从机板和三个逻辑设备。设备 1、2 在同一块安路从机板上；设备 3 使用 Zynq-7020 板并仅接入 RS485。CAN 为独立总线，三台逻辑设备共享一条 RS485 半双工总线。

## 二、目录说明

### `01_FPGA_MAIN` 安路主机硬件及启动参考

- `IGW_GATEWAY_R15_CORE/`：安路主机 TD 工程、PL RTL、接口约束与仿真源码。工程入口为 `project_r15/soc_prj/soc_prj.al`；源码和连接关系检查为 `project_r15/check_project.py`。
- `IGW_R15_BOOT_VM/`：R15 历史启动镜像构建脚本及参考输入，包括位流、FSBL、U-Boot 等文件。
- `IGW_R15_DTB_FIX1/`：R15 设备树修复源码及参考 DTB。
- `IGW_R15_BRINGUP_VM/`：主机 CAN 旁路采集程序及运行检查脚本。
- `CAN_Driver_Reference/ipms_canfd.c`：原始 IPMS CAN 控制器 Linux 驱动参考源码，保留原版权和许可证声明；尚未与现场正在运行的驱动二进制逐字节比对。

这些硬件、启动和驱动资料保留原始版本关系，不能仅凭目录名称认定它们与现场当前 BOOT、DTB 或驱动完全一致。查看或重建时应核对目标板、版本及哈希，不要直接把参考文件当作通用烧录包。

### `02_MONITOR` 监控主机、NPU 和安路从机

- `IGW_NPU_Secure_R12_2/`：监控、CAN、RS485、AES-GCM 归档和 NPU 模型的可编译基线。`npu/src/` 为推理程序，`npu/generated/` 为冻结模型数据；`src/igw_vault.c` 为加密归档程序。
- `IGW_Multi_Device_R13/`、`IGW_R16_CAN_UPLINK_SOURCE/`、`IGW_R17_RS485_UPLINK_SOURCE/`、`IGW_R17_1_ACCOUNTING_FIX_SOURCE/`：多节点、CAN 上报、RS485 上报及计数兼容的历史构建基线。
- `IGW_R26_BIDIRECTIONAL_DEMO/`：R26 主机程序、监控网页、NPU 模型、安路从机 R26 源码及测试。`slave_r17/`、`baseline_r9/` 是基线和回归材料，实际使用的 R26 从机代码在 `slave_r26/`。
- `IGW_R26_UI_REFRESH/`：监控网页与文件网页的界面整理版本。
- `IGW_ZYNQ_RS4853/`：设备 3 RS485 数值通信及监控网页扩展。
- `IGW_ZYNQ_RS4853_UPLINK/`：设备 3 RS485 一次授权上报及监控网页扩展。其主机编译脚本使用 `host/monitor/src/igw_web.c` 和 `tests/host_fixture/src/igw_rs485_rw.c`，`host/run_web_gateway.sh` 组织相关进程。

### `03_FILE_TRANSFER` 文件网页及设备 3 完整文件工程

- `IGW_R23_SD_FILES/`：安路设备 1、2 的 SD 文件服务源码及主机文件传输基线，其中 `slave_r23/` 是 R23 基线；不要与 R26 从机的 `main.c` 混编。
- `IGW_ZYNQ_RS4853_FILE/`：设备 3 的 Vivado 可重建工程脚本、PL 源码、约束、仿真，以及 Vitis standalone 文件服务和 TF 卡代码。`zynq/create_project.tcl` 为 Vivado 工程创建入口，`zynq/src/` 为 Vitis 应用源码。
- `IGW_CAN1_RS3_PARALLEL/`：最终定向并行文件服务源码及网页。当前实现允许 **CAN 设备 1 与 RS485 设备 3** 的两个文件任务协同；其他并行组合受版本自身限制。该目录也保留匹配的设备 3 源码和模拟测试。

### `90_REFERENCE` 历史参考

- `IGW_ZYNQ_CAN3/`：早期 Zynq CAN 设备 3 方案，供查阅，不是当前 RS485 设备 3 演示配置。
- `IGW_R17_2_SLAVE_QUIET/`：早期安路从机安静版本基线，不是 R26 文件功能的最终从机源码。

## 三、开发环境与构建入口

1. **安路主机硬件**：使用与 DR1M90 板卡匹配的安路 TD 和厂商 IP。打开 `IGW_GATEWAY_R15_CORE/project_r15/soc_prj/soc_prj.al`。先按其 `README_FIRST_CN.md` 核对工程、约束、设备树和时序。
2. **安路 Linux 主机程序**：在 Ubuntu 中使用 AArch64 Linux 交叉编译器。原脚本默认查找 `~/uisrc-lab-anlogic/tools/aarch64-linux/bin/aarch64-linux-gnu-`；部分文件服务脚本可通过 `IGW_AARCH64_CC` 指定编译器。NPU 构建还需要原版 Buildroot/ArmNN SDK，归档运行需要板端 OpenSSL 1.1 系列兼容库。
3. **安路从机**：使用原安路 SDK、BSP、链接脚本及原工程配置。R26 使用 `IGW_R26_BIDIRECTIONAL_DEMO/slave_r26/slave/main.c`；相关拆分源码、生成脚本和校验文件位于同一 `slave_r26/` 目录。
4. **Zynq 设备 3**：使用 Vivado 和 Vitis 2020.2。在 Vivado Tcl Console 中运行 `IGW_ZYNQ_RS4853_FILE/zynq/create_project.tcl`，按原包说明运行数值逻辑、发送保护仿真及构建脚本，生成对应 XSA；Vitis standalone 平台需启用 `xilffs`，再导入 `zynq/src/` 源文件。
5. **文件并行服务**：在 Ubuntu 进入 `IGW_CAN1_RS3_PARALLEL/`，运行 `bash host/build_on_ubuntu.sh`。脚本生成 AArch64 文件服务程序；设备 1 的安路从机与设备 3 的 Zynq 文件固件仍需按各自工程构建。

监控升级链和文件升级链是增量版本。相关脚本会检查已有目录及校验和：监控从 R12.2、多节点/R15、R16、R17、R17.1、R26、界面更新进入设备 3 扩展；文件从 R23、界面更新、设备 3 文件扩展进入定向并行版。各包的 `README` 及安装脚本给出了各自的前置目录。原主机 SD 卡上保留的部署基线、完整厂商 SDK、交叉编译器、Buildroot 根文件系统和现场独立工程不在本包中，因此本包不等同于整板镜像或一键恢复包。

## 四、版本和验证范围

- 用户此前实板记录包括设备 1、2 的 CAN／RS485 访问和跨总线读写，设备 3 的 RS485 读写与受控上报；设备 3 文件服务有 4096 字节下发、读回和生成取回记录。
- 用户随后报告在设备 3 的 RS485 文件任务期间启动设备 1 的 CAN 文件任务；网页记录显示两个 65536 字节任务均校验通过。该结果对应指定设备组合；不同组合、长期稳定性及断电恢复不能由此直接推定。
- NPU 已接入正常、CAN 链路异常、RS485 链路异常三类通信状态模型；数值核验和故障分类准确率是不同的评价指标。
- 本次整理仅在本机重新运行源码检查和模拟测试，结果见 `本次整理与检查.md`；没有重新连接目标板、执行 TD/Vivado 实现或交叉编译目标固件。
- 原发布包中的验证状态、路径与部署说明反映各自发布时刻；后续实板记录以相应会话为准，不应把历史版本自动当作当前全部功能的板测证明。

## 五、文件完整性

每个原工程内保留原有 `SHA256SUMS.txt`、`SOURCE_SHA256SUMS.txt` 或 `PACKAGE_SHA256SUMS.txt`。本包根目录的 `SHA256SUMS.txt` 为整理后全部文件的摘要；`SOURCE_ARCHIVES_SHA256.tsv` 记录整理所用原始源码包的摘要。解压后可在 Linux 进入本目录运行：

```sh
sha256sum -c SHA256SUMS.txt
```

为避免 Windows 工具因目录过长出错，建议解压到较短路径，例如 `C:\IGW_Code`。
