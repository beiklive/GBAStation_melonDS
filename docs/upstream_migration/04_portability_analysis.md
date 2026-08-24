# 可移植性分析

## [A] 完全可移植

适合以小补丁、配合行为测试迁移：ARM 解释器和 JIT 修复；内存映射/失效；DMA 时序；定时器/IRQ；卡带协议和存档逻辑；兼容 savestate 的核心改动；DS WiFi 设备行为；SPU 混音/输出队列修复；DSi SCFG/BIOS/直接启动逻辑；DSi DSP-HLE/LLE；不依赖主机 API 的软件 renderer 准确性规则。

Switch fork 已经包含其中很多模块。“可移植”不等于整文件替换，而是共享经过测试的行为。

## [B] 核心可移植，但需要 Switch backend

* **网络：**主线 Local-MP/LAN/Netplay 协议和分包代码可以复用，但当前 `Platform::MP_*`、`LAN_*` 都是桩；需要 libnx socket、轮询、错误处理、退出和 UI 策略。
* **Camera/Microphone：**DSi 设备时序/状态可移植，采集必须使用 Switch 来源；当前麦克风只有静音/合成噪声。
* **NAND/SD/FAT：**设备逻辑可移植，路径、容量、写入/刷新和恢复行为要经过 Switch 存储 helper。
* **输入抽象：**映射/状态修复可移植，但应保留 libnx HID、触摸、模拟光标和六轴代码。

## [C] Renderer 可移植，但需要图形后端

多边形设置、裁剪、插值、深度、Alpha、雾、边缘标记、抗锯齿、纹理解码/缓存和 framebuffer 语义可以作为算法改动审查。每个迁移应拆成共享 GPU 行为和 deko3D 实现；主线 OpenGL context/shader 不能通过 Switch 的空 GL-proc 后端运行。

## [D] 保留 Switch 实现

保留 `GPU2D_Deko`、`GPU3D_Deko`、DKSH/RomFS 打包、deko3D barrier 和资源布局、自定义 Gfx/UI、Applet 生命周期、audren/audrv 线程、HID/触摸/六轴映射、Switch 文件搜索顺序、超频和帧率控制。

## [E] 不需要移植

不移植 Qt 对话框/资源、SDL 桌面循环、WGL/GLX/EGL/AGL context、X11/Wayland、桌面窗口/音频/输入 provider 或桌面调试 UI。GDB 是开发功能，不是 NRO 发布前提。

## 安全门槛

1. 建立补丁台账，禁止整目录覆盖。
2. JIT 必须测试 ARM/Thumb、branch、exception、映射/非对齐内存、自修改代码、失效和线程生命周期。
3. 每个 GPU 准确性补丁都要比较软件参考输出和 Deko 输出。
4. 存储/网络必须测试错误路径、挂起恢复和媒体丢失。

