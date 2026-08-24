# Renderer 分析

## 当前 Switch renderer

Switch 构建设置 `ENABLE_OGLRENDERER=OFF`、`ENABLE_DEKOGPU=ON`。`GPU::Init()` 创建 `GPU2D::DekoRenderer`；renderer 0 创建 `GPU3D::DekoRenderer`，`frontend/switch/main.cpp` 选择 renderer 0。`Platform::GL_GetProcAddress` 返回 `NULL`，因此没有桌面 OpenGL context。

`GPU2D_Deko.cpp` 使用 deko3D image、descriptor、command buffer 和 barrier 处理背景、精灵、窗口、显示捕获和合成。`GPU3D_Deko.cpp` 加载 DKSH pass，完成 span 插值、分箱、深度/混合、纹理光栅化、雾、边缘标记、阴影和抗锯齿。CMake 用 `uam` 编译 GLSL，并将 DKSH 放入 NRO 的 `romfs/shaders`。

这是 Switch 专用的生产路径，属于 [D] 保留，而不是应被 OpenGL 替换的旧实现。

## 软件 renderer

两边都保留软件 2D/3D。主线围绕 `GPU_Soft` 重组，并且 `GPU2D_Soft`、`GPU3D_Soft` 差异很大。整文件复制风险高，应按行为拆分：

* 多边形设置/裁剪和透视插值；
* 深度、Alpha、雾、边缘标记和抗锯齿；
* 纹理解码、调色板和 framebuffer 语义；
* 不依赖桌面 API 的时序/线程安全改动。

只改变模拟语义时属于 [A]；若必须同步 Deko shader、脏数据上传、image layout 或同步规则，则属于 [C]。`Soft_Threaded` 字段不代表 Switch 正在使用 threaded software renderer，默认路径仍是 Deko。

## 主线 OpenGL/Compute

主线启用 `ENABLE_OGLRENDERER` 时构建 `GPU2D_OpenGL`、`GPU3D_OpenGL`、`GPU3D_Compute`、纹理缓存和 OpenGL shader。这些可以参考状态转换和算法，但桌面 context/loader/pipeline 不能直接用于 Switch。

| 层次 | 决策 |
| --- | --- |
| GPU 寄存器语义、纹理解释 | 作为 [A] 核心逻辑审查 |
| 软件参考行为 | 带视觉测试小范围移植 [A] |
| OpenGL context、GL shader 打包、桌面同步 | 原样属于 [E] |
| 等价 deko3D shader/资源改动 | [C]，重新实现 Switch 后端 |
| 现有 Deko renderer 和 compositor | [D]，保留 |

## 安全顺序

1. 为当前 Deko/软件路径建立确定性的截图或 hash 测试。
2. 移植一个软件/CPU 侧准确性修复并比较输出。
3. 检查 Deko 是否已经表达相同规则。
4. 如没有，再实现等价 DKSH/deko3D 改动并测试 barrier、脏上传和 framebuffer layout。
5. 在考虑 renderer 架构替换前先做实机性能测试。

