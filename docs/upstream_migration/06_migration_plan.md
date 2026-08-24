# 迁移计划

## 最终结论

1. Switch fork 是一个较早但完整的 DS/DSi 核心，包含 ARM 解释器、ARM64 JIT、Teakra、WiFi 设备模拟、存档和自定义前端。
2. 生产图形后端是 deko3D 2D + deko3D compute/graphics 3D，不是 OpenGL。
3. 两边属于同一 ARM64 JIT 家族，但 JIT/内存文件有差异，不能视为等价。
4. 主线主要领先在 DMA 时序、卡带/存储重构、DSi NAND/I2S/DSP-HLE/直接启动、FAT、Mic/Camera 接口、网络栈和累积 bug fix。
5. 应先同步核心准确性，同时保留 Switch renderer 和平台代码。
6. CPU、内存、DMA、IRQ、卡带/存档、SPU、DSi 逻辑和软件 renderer 语义可在测试后移植。
7. 网络、Camera/Mic 采集、SD/NAND/FAT 需要 Switch backend。
8. deko3D、libnx 生命周期/音频/HID/文件系统、RomFS shader、UI 和帧率控制必须保留。
9. Qt/SDL、桌面 OpenGL、X11/Wayland 和桌面调试 UI 不需要迁移。
10. 建议继续维护 Switch fork，并通过小补丁逐步让核心靠近 upstream。

## P0：必须同步的核心能力

1. 建立 upstream-to-fork 补丁台账，并为 ARM/Thumb JIT、解释器、内存映射、DMA、定时器、IRQ、卡带传输/存档和 savestate 建立回归测试。
2. 语义审查 `ARMJIT.cpp`、`ARMJIT_Memory.cpp` 和 `ARMJIT_A64/*`；保留 ARM64/libnx 假设，禁止整目录替换。
3. 引入 `DMA_Timings` 行为和相关内存/时序修复。
4. 移植卡带地址回绕和传输冻结修复；之后再考虑 `NDSCart/` 分层架构。
5. 适配 SPU 输出泄漏和启动状态修复。
6. 每次只移植一个 GPU 准确性修复，同时保持 Deko 路径。

## P1：强烈建议同步的核心能力

1. 更新 DSi SCFG/BIOS/直接启动逻辑，再按依赖顺序加入 I2S、NAND、FAT 和存储状态。
2. 在现有 Teakra 状态处理旁加入 DSP-HLE。
3. 审查软件 renderer 准确性改动，仅在 Deko 缺失对应行为时同步 shader。
4. 将 Action Replay 数据库/核心改进与桌面 UI 分离。
5. 增加 Camera/Mic 核心状态，设备接入放到 P2。

## P2：需要 Switch backend 的工作

* 用 libnx socket 实现 `Platform::MP_*` 和 `LAN_*`，再接入主线 Local-MP/LAN/Netplay 逻辑，并测试错误/挂起恢复。
* 提供 Switch Camera/Microphone 来源；若暂时没有，应明确禁用，而不是导入桌面采集代码。
* 通过 Switch 文件 helper 增加受控的 SD/NAND/FAT 路径、容量、写入/刷新和恢复策略。
* 保留 audren/audrv 和 HID 后端，只采用兼容的核心音频/输入接口。

## P3：Renderer 评估

把主线 GL/Compute 架构作为参考，不作为即插即用代码。优先建立渲染测试、Deko 等价实现、barrier/资源验证和实机性能数据。不要用桌面 OpenGL 替换现有 `GPU2D_Deko`/`GPU3D_Deko`。

## 明确排除

Qt widget、SDL 桌面循环、平台 GL context、桌面音频/输入、窗口管理器和桌面 UI 不应进入 Switch 迁移。

