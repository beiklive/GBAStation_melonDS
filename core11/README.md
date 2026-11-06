# GBAStation NDS 1.1

独立的 Switch 核心，复用 GBAStation 的暂停菜单、输入、音频、滤镜和双屏布局。CPU、JIT、卡带、GPU、音频和即时存档来自 Pokémon Fulldex 的 melonDS 1.1（基准提交 `c255f97b7be1e8b5ebe8f792cf01b69660d74016`）。普通 DS ROM 直接交给 melonDS，不套用针对发行版 ROM 地址的游戏补丁。硬件绘制 1–10 倍显示层，保留 DS 原生像素处理透明混合和亮度特效。关闭补帧。

## Switch 测试

将 `GBAStationNDS11.nro` 放到 SD 卡的 `/switch/GBAStationNDS11/`，以应用模式进入 hbmenu。无启动参数时使用日志里的 `/roms/nds/black.nds`；GBAStation 传入 ROM 和 `--return` 时仍按启动参数运行。BIOS、固件和按键使用 GBAStation 已有配置。默认遵循现有 JIT 设置，启动时按住 ZL 可仅对这次运行强制使用解释器。

首次运行会尝试复制 GameDB 指定目录中的同名 `.sav`，之后在该目录的 `core11/` 内保存。即时存档也写入状态目录的 `core11/`，与 v8 槽位分开。要接着已有进度玩，应从游戏标题画面读卡带存档。如果只有旧即时存档，先在原核心用转场前的槽位进入游戏并用游戏菜单保存。已经初始化过新版进度的情况下，请先备份新版的 `.sav`，不要覆盖它。运行日志在 `/GBAStation/log/NdsCore11.log`，会记录执行模式、ARM9/ARM7 PC、亮度和两屏图像校验值。

存档初始化使用分块读写，避免 Switch 的 `copy_file` 权限与 inode 兼容问题。旧版失败留下的 `.seed` 会在重试时重写，已有 `.sav` 或 `.bak` 不会被初始化覆盖。复制失败日志会标明具体步骤和路径。Mac 回归检查：`c++ -std=c++17 -Wall -Wextra -fsanitize=address,undefined tests/core11_save_seed.cpp -o /tmp/core11_save_seed && /tmp/core11_save_seed`。

Switch 的原生分辨率 3D 和高分辨率 3D 均固定使用供体的 deko3D 路径，包括 1 倍时的原生画面。模拟主线程运行在 CPU 0，GPU 提交和取回线程使用供体的 CPU 1/2 调度。原生 GPU 输出仍通过 DS 的 2D 合成、亮度和屏幕捕获流程；读写即时存档、切换倍率和退出前会等待原生与高分辨率 GPU 工作完成。Mac 回放使用多线程软件绘制。

显示合成输入使用 CPU 缓存并在提交 GPU 前刷新。启动日志应显示 `native3D=deko3D enhanced3D=deko3D`。每 240 帧记录 `[core11-perf]`（`native=gpu`、模拟总耗时及其中的 3D 等待）和 `[core11-video]`（前帧等待、像素上传、命令提交），单位均为每帧毫秒；`renderCores` 仅统计原生软件绘制线程，GPU 模式下为零。

`NativeRenderer.cmake` 在构建目录生成供体原生渲染器的副本，仅补齐新建会话时的多边形列表计数与首帧标志初始化，防止重新启动游戏后线程读取旧会话指针；不修改供体工作树或 JIT。Mac 多线程回归：`cmake --build build_core11_mac --target render_thread_check`，然后 `build_core11_mac/render_thread_check 游戏.nds`，测试使用独立临时存档。`CORE11_RENDER_THREAD=0` 可让 Mac `replay` 使用串行绘制做对照，不是 Switch 菜单选项。

菜单“画面设置”新增“快进声音保持原速”，默认关闭，保存到 `fastforward.normal_speed_audio`。关闭时加速音频随实际运行速度升调；开启时游戏继续快进，声音按正常速度与音高播放。每个模拟帧将 PCM 移入前端队列，避免核心的小环形缓冲在快进时溢出。原速声音最多积压 30 秒，满时丢弃新音频；音效会落后于画面，退出快进清掉积压并恢复当前声音。输出只提交真实的 PCM 采样，不将不足 768 帧的数据用末尾采样补长；每次音频服务唤醒补充所有可用缓冲槽，音频线程在 CPU 2 运行。`[audio]` 日志记录实际提交的 pitch、有效采样帧数、短块、空读和被拒绝的缓冲块；空读不等同于设备断音。Mac 信号回归：`c++ -std=c++17 -Wall -Wextra -fsanitize=address,undefined tests/nds_audio_pcm.cpp -o /tmp/nds_audio_pcm && /tmp/nds_audio_pcm`。

与供体前端仍有的区别：未接入模拟/显示分线程流水线与提前一帧显示调度；加速时虽只显示最后一帧，但尚未启用跳过中间帧的 2D/高分辨率绘制标志；音频使用 GBAStation 的 audren 输出，未接入 WSOLA；新增的原速播放开关采用有界积压队列；游戏专用静态重编译、地图/场景宽屏补丁及合成布局未接入。补帧按要求关闭。上述前端差异不代表仍在使用旧版 CPU/JIT 或原生 3D 核心。

## 构建

从仓库根目录运行 `bash switchbuild.sh --core11`。使用 devkitPro Switch 工具链、Ninja 和 CMake。优先使用旁边的 `pokemon-fulldex` 源码，也可用 `FULLDEX_SOURCE` 指定工作树；没有本地源时下载固定提交。输出位于 `build_core11_switch/GBAStationNDS11.nro`。

Mac 回放使用同一套核心和会话代码，使用软件绘图，可检查 JIT/解释器和状态格式。测试应传入单独的电池存档副本。

```sh
cmake -S core11 -B build_core11_mac -G Ninja -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Release -DFULLDEX_SOURCE="$PWD/../pokemon-fulldex"
cmake --build build_core11_mac --target replay
build_core11_mac/replay BIOS目录 游戏.nds 临时副本.sav 150 jit
```

也可在最后传入 v13 原始状态文件和倍率。该工具不会读取旧版 v8 即时存档。

## 加载与金手指回归

0 字节电池存档按尚无 SRAM 处理，不再导致加载失败；加载器不改写原文件。BIOS、固件、ROM、非空存档的读取失败会通过 `[core11-load] FAILED` 输出路径和原因。

usrcheat 优先匹配游戏代码和头部 CRC。没有精确版本或相同 CRC 存在多个版本时，在原金手指菜单中按数据库版本分目录，初始全部关闭；启用一个版本的代码会关闭其他版本的代码，避免混用。不会再悄悄采用同游戏代码的第一条。`[core11-cheats]` 记录实际启用的名称、词数和代码开头；首条 32 位相等条件也记录当前内存值。

`cmake --build build_core11_mac --target cheat_check` 后运行 `build_core11_mac/cheat_check 游戏.nds [usrcheat.dat]`。测试在独立临时目录验证空存档启动、JIT/解释器的 VBlank 写入和关闭代码；重装机兵 3 的数据库还验证“亿万长者”和 SELECT 触发代码。不会修改用户存档。
