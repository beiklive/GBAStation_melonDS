# 当前 Switch 版本架构

## 范围

本文基于 `GBAStation_melonDS` 的 `831b7c7`（2026-01-13）源码，以及已成功完成的 MSYS/devkitPro NRO 构建结果。两个本地仓库没有共同 Git merge-base，因此不能把主线目录直接覆盖到 Switch 版本。

## 运行架构

```text
frontend/switch/main.cpp（libnx 入口、UI、帧循环）
  -> NDS 核心（ARM9/ARM7、内存、DMA、定时器、IRQ、卡带、DSi）
  -> GPU 调度
       -> GPU2D::DekoRenderer
       -> GPU3D::DekoRenderer
       -> deko3D 命令缓冲区与 DKSH Shader
  -> SPU 输出队列 -> libnx audren/audrv 音频线程
```

`src/CMakeLists.txt` 构建核心静态库，包含 ARM 解释器/JIT、DS/DSi 模拟器、Teakra、GPU 软件代码、SPU、WiFi 设备模拟、存档和卡带。`src/frontend/switch/CMakeLists.txt` 链接 Switch 前端并将其打包为 `melonDS.nro`。

## 核心模块

| 模块 | 实现与结论 |
| --- | --- |
| ARM9/ARM7 | `ARM.cpp`、`ARMInterpreter*.cpp`、`CP15.cpp`、`NDS.cpp`；使用共享 DS 调度器、DMA、定时器和 IRQ。 |
| JIT | `ARMJIT.cpp`、`ARMJIT_Memory.cpp`、`ARMJIT_A64/*`、`dolphin/Arm64Emitter.cpp`；Switch 为 ARM64，CMake 会启用 ARM64 JIT。ARM/Thumb 的 block compiler、ALU、load/store、branch、失效处理和汇编链接代码均存在。 |
| 内存/DMA/时序 | `NDS.cpp`、`DMA.cpp`、`ARM.cpp`、CP15；包含 DSi 分支，但还没有主线的 `DMA_Timings` 拆分。 |
| 卡带/存档/作弊 | `NDSCart.cpp`、`NDSCart_SRAMManager.cpp`、`GBACart.cpp`、`SPI.cpp`、`Savestate.cpp`、AR 文件。主线后来拆分了卡带类型。 |
| DSi | 已有 AES、Camera、DSP、I2C、NDMA、NWifi、SD、SPI/TSC；主线新增 I2S、NAND、FAT/存储和 DSP-HLE。 |
| 音频核心 | `SPU.cpp` 通过 `SPU::ReadOutput` 提供采样；输出后端独立于核心。 |

## Switch 实际图形后端

构建使用 `ENABLE_OGLRENDERER=OFF`、`ENABLE_DEKOGPU=ON`。

* `GPU::Init()` 直接创建 `GPU2D::DekoRenderer`。
* `GPU::InitRenderer(0)` 在 `DEKOGPU_ENABLED` 下选择 `GPU3D::DekoRenderer`；`frontend/switch/main.cpp` 使用 renderer 0。
* `GPU2D_Deko.cpp` 负责背景、精灵、窗口、显示捕获和合成。
* `GPU3D_Deko.cpp` 使用 DKSH compute/graphics pass 完成插值、分箱、深度/混合、光栅化、雾、边缘标记和抗锯齿。
* CMake 用 `uam` 编译 GLSL，并把 `romfs/shaders/*.dksh` 打包进 NRO。

软件 2D/3D renderer 仍作为后备和参考代码存在，但默认 Switch 路径不使用 threaded software renderer。OpenGL 仅条件编译；`Platform::GL_GetProcAddress` 返回 `NULL`，因此 Switch 没有 GL context。

## Switch 平台边界

| 服务 | 源码行为 |
| --- | --- |
| 线程/计时 | `frontend/switch/Platform.cpp` 使用 libnx 线程、互斥锁、条件变量和 `svcSleepThread`；工作核心按创建顺序分配。 |
| 音频 | `main.cpp` 的专用线程消费 `SPU::ReadOutput`，使用两个 PCM 缓冲区和 libnx `audren`/`audrv`，输出 48 kHz。 |
| 输入 | libnx HID 按键、模拟触摸光标、实体触摸屏、六轴传感器、合盖状态和快捷键；麦克风目前是静音/合成噪声。 |
| 文件 | `OpenLocalFile` 依次搜索 `/switch/melonds/`、`/melonds/` 和调用者路径；`romfsInit()` 提供打包资源，设置和输入使用 `sdmc:/switch/melonDS/`。 |
| 固件/DSi 媒体 | 配置了 BIOS、Firmware、NAND、SD 相对路径；存在 `sd.bin` 时启用 DSi SD。 |
| 网络 | `Platform::MP_Init`/`LAN_Init` 返回 false，收发函数为空操作；主机本地联机/LAN 尚未实现。 |
| UI/生命周期 | 自定义 ROM 浏览器、存档快捷键、Applet hook、超频/性能模式和 deko3D 合成器。 |

自定义 Switch 前端、deko3D renderer、libnx 音频/HID/文件系统、RomFS 打包和帧率控制属于平台专用实现，同步主线核心时必须保留。

