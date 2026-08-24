# OpenGL/NanoVG 移植第一阶段

## 当前状态

OpenGL/NanoVG 前端暂时停用。当前提交优先保证 Deko3D/nds_stub 迁移版本稳定，Switch 配置会强制关闭 `ENABLE_OGLRENDERER`，因此不会查找或链接桌面 `epoxy`/OpenGL 依赖。

## 已保留内容

- `src/frontend/switch_gl/CMakeLists.txt` 和迁移文档仍保留，后续恢复 OpenGL 时可继续使用。
- 原 Deko3D 前端仍保留，默认构建行为不变。

## 构建

当前在 MSYS2 shell 中执行：

```sh
./switchbuild.sh --clean -j 8
```

当前生成原来的 `build_switch/melonDS.nro`。`--opengl` 参数暂时会被拒绝。

OpenGL 前端默认从仓库同级目录查找 `BeikLiveStation`。如果目录不同，可在 CMake 配置时指定：

```sh
-DBEIKLIVE_STATION_DIR=/path/to/BeikLiveStation
```

## 当前边界

本阶段先验证 Context、GL 函数加载、melonDS OpenGL 合成器、双屏纹理显示和 NanoVG 叠加层。RetroArch GLSL 链和完整菜单迁移安排在下一阶段。
