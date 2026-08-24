# 主线版本架构

## 范围

本文分析本地 `example_melonDS` 的 `906e9eb`（2026-08-23），重点是模拟器核心；Qt/SDL 文件只用于识别哪些内容不应移植到 Switch。

## 核心结构

主线保留 ARM 解释器/JIT 和 Teakra，但显著扩展了核心构建。`src/CMakeLists.txt` 新增或接入：

* `DMA_Timings.cpp`，集中处理 DMA 时序；
* `DSi_I2S`、`DSi_NAND`、`FATIO`、`FATStorage` 和 FatFs；
* `NDSCart/` 下的 retail、homebrew、R4、SD、NAND、IR、BT 卡带类；
* `SPI_Firmware`、`FreeBIOS`、`ROMList`、`Utils`、`Mic`；
* 与 Teakra 并存的 DSP-HLE AAC/G711/Graphics ucode；
* `GPU_Soft`、OpenGL 2D、OpenGL/Compute 3D 和纹理缓存模块；
* 可选 GDB stub，以及 Local MP、LAN、Netplay、pcap、slirp 驱动。

## 主要功能差异

| 模块 | 主线现状 | 对 Switch 迁移的意义 |
| --- | --- | --- |
| CPU/JIT | ARM64 后端家族相同，但 JIT/内存文件有差异 | 只能选择性移植行为修复。 |
| DMA | 独立的 `DMA_Timings` | 高价值、可移植的准确性改进。 |
| 卡带 | 新的卡带类层次和更多设备 | 收益大，但需要分阶段结构迁移。 |
| DSi | NAND、I2S、FAT、直接启动/SCFG、DSP 改进 | 核心可移植，文件/设备后端需重接。 |
| 音频 | SPU 修复和平台输出抽象 | 移植核心修复，保留 libnx 输出。 |
| Camera/Mic | 核心设备接口 | 真实采集属于平台能力。 |
| 图形 | 软件 renderer 重组、新 OpenGL 2D、OpenGL 3D/Compute、纹理缓存 | 可参考算法，桌面 GL 后端不能直接运行。 |
| 网络 | Local MP、LAN、Netplay 和 socket/slirp/pcap 驱动 | 协议核心可复用，Switch 传输后端缺失。 |
| 调试 | 可选 GDB stub 和构建信息 | 开发功能，不是 NRO 发布前提。 |

Fork 日期之后的主线历史包含卡带地址回绕、卡带传输冻结、SCFG_EXT/BIOS 启动、SPU 输出缓冲泄漏、启动静音和新的 OpenGL 2D renderer 修复。这些应逐项评估，不能据此整仓库合并。

## 平台边界

主线 `Platform.h` 暴露文件、计时、线程、Camera、Microphone、网络和 OpenGL 回调。Qt/SDL 及 WGL、GLX、EGL、AGL、X11、Wayland context 负责桌面实现；它们不是模拟器核心，不应直接移植到 Switch。Switch 应通过 libnx 实现所需回调，并保留 deko3D 前端。

## JIT 结论

两边都包含 ARM/Thumb block compiler、ARM64 emitter、ALU、load/store、branch 和 linkage。它们属于同一 JIT 家族，但不能确认等价：核心和 JIT 文件存在实质差异。所有主线 JIT 修复都应使用 ARM/Thumb、branch、exception、自修改代码、cache invalidation 和线程生命周期测试后再采用。

