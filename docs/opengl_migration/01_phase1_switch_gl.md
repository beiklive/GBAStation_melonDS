# OpenGL/NanoVG 移植第一阶段

## 已完成内容

- 新增 `src/frontend/switch_gl` 独立前端，不再依赖 Deko3D 的 UI 绘制代码。
- 复用 BeikLiveStation/Borealis 中已验证的 glad 和 NanoVG OpenGL 实现。
- Switch OpenGL 构建时，melonDS 3D 使用 `GPU3D::GLRenderer`，2D 暂时使用软件渲染器。
- 通过 `GPU::GLCompositor` 将上下屏合成为 OpenGL 纹理，并绘制到窗口 framebuffer。
- 初始化 NanoVG，并加入基础叠加层，后续可在此基础上迁移菜单和设置页面。
- `Platform::GL_GetProcAddress()` 在 OpenGL 构建中连接到 `eglGetProcAddress()`。
- 原 Deko3D 前端仍保留，默认构建行为不变。

## 构建

在 MSYS2 shell 中执行：

```sh
./switchbuild.sh --opengl -j 8
```

该模式生成 `build_switch/melonDS_gl.nro`。默认不带 `--opengl` 时仍生成原来的 `melonDS.nro`。

OpenGL 前端默认从仓库同级目录查找 `BeikLiveStation`。如果目录不同，可在 CMake 配置时指定：

```sh
-DBEIKLIVE_STATION_DIR=/path/to/BeikLiveStation
```

## 当前边界

本阶段先验证 Context、GL 函数加载、melonDS OpenGL 合成器、双屏纹理显示和 NanoVG 叠加层。RetroArch GLSL 链和完整菜单迁移安排在下一阶段。
