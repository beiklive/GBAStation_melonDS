# 功能差异矩阵

以下结论基于两个仓库当前源码。“已有”表示代码已接入相关构建/运行路径，并不代表所有游戏都完成实机验证。

| 模块 | GBAStation_melonDS | example_melonDS | 主线新增/改进 | Switch 状态 | 迁移动作 | 分类 |
| --- | --- | --- | --- | --- | --- | --- |
| ARM9/ARM7 | 解释器和共享调度器 | 持续维护的解释器/调度器 | 累积准确性修复 | 已有 | 选择性同步 | A |
| ARM64 JIT | ARMJIT_A64 block compiler | 同一家族但文件有差异 | JIT/内存修复 | 已有 | 语义级同步 | A |
| 内存 | 较早的映射/JIT 内存代码 | MemRegion/MemConstants 组织 | 映射和失效改进 | 已有 | 分阶段重构 | A |
| DMA/时序 | 单体 DMA | `DMA_Timings` 拆分 | 时序准确性 | 已有 | 带测试迁移 | A |
| 定时器/IRQ | NDS/ARM 核心实现 | 持续维护 | 累积修复 | 已有 | 选择性同步 | A |
| GPU 2D | Deko2D + 软件代码 | 软件和新 OpenGL 2D | GL pipeline/准确性 | Deko | 保留 Deko，移植逻辑 | C/D |
| GPU 3D | Deko compute/graphics + 软件后备 | 软件、GL、Compute | 纹理缓存和 renderer 改进 | Deko | 保留后端，审查算法 | C/D |
| Threaded software | 有设置但非默认 | 偏桌面调度 | 无 Switch 后端 | 未使用 | 暂缓 | C |
| OpenGL/Compute | 条件源码，无 Switch GL context | OpenGL/Compute pipeline | 桌面 shader/backend | 无 | 不复制后端 | E/C |
| 音频核心 | SPU 队列 | SPU 修复和平台抽象 | 泄漏/启动行为 | 已有 | 移植核心修复 | A |
| 音频输出 | libnx audren/audrv 线程 | 桌面输出 | 主机后端差异 | 已有 | 保留 libnx | D |
| 输入 | libnx HID/触摸/动作感应 | 桌面抽象 | 输入/UI 改进 | 已有 | 保留后端，移植核心 | B/D |
| 存档/状态 | SRAM manager 和 savestate | 新卡带/存储依赖 | 兼容性和功能 | 已有 | 随卡带分阶段迁移 | A |
| Cheat | AR 支持和 Switch 快捷键 | 数据库/导入/UI 扩展 | 更多数据库/UI | 基础支持 | 只移植核心 | A/E |
| WiFi 设备 | DS WiFi 模拟 | 持续维护 | 准确性修复 | 已有 | 选择性同步 | A |
| Local MP/LAN | Platform 方法为桩 | `net/` 网络栈和 netplay | 真实主机网络 | 无 | 增加 libnx socket | B |
| DSi | AES/Camera/DSP/SD/NWifi | NAND/I2S/FAT/直接启动/SCFG | 更完整 DSi | 部分已有 | 分阶段同步 | A/B |
| DSi DSP | Teakra 路径 | Teakra + DSP-HLE | AAC/G711/Graphics HLE | 部分已有 | 移植核心/状态 | A |
| Camera | 核心存在，无采集后端 | Platform capture | 设备支持 | 部分已有 | 需 libnx 来源 | B |
| Microphone | 噪声/静音注入 | Mic callback/core | 真实采集抽象 | 部分已有 | 可选 Switch 后端 | B |
| SD/MMC/NAND | 旧路径/逻辑 | FAT storage/NAND 类 | 真实存储行为 | 部分已有 | Switch 文件适配 | B |
| 卡带设备 | 单体卡带代码 | Retail/SD/R4/IR/BT/NAND 类 | 功能大幅扩展 | 部分已有 | 分阶段重构 | A/B |
| 调试器 | Switch 未构建 | 可选 GDB stub | 开发工具 | 无 | 可选 | E |
| Qt/SDL/桌面 | 非运行后端 | 完整桌面前端 | UI/平台内容 | 不适用 | 忽略 | E |

分类：A=完全可移植；B=核心可移植但需要 Switch backend；C=renderer 算法可移植但需要图形后端；D=保留 Switch 实现；E=不需要移植。

