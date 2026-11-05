# Switch 实验性 3D 渲染

将 `GBAStationNDSStub.nro` 放在 SD 卡的 `/GBAStation/core/`。3D 硬件渲染始终开启；旧配置中的渲染器开关不再生效。

- 在核心菜单的 **画面设置 → 3D分辨率** 用左右键选择 1–10 倍，会保存选择，并在返回游戏时应用最后选定的倍率。6× 为 1536×1152，10× 为 2560×1920，均为单块 DS 屏幕的内部渲染尺寸，独立于电视或掌机输出尺寸。
- 显示缓冲按需扩容，调低倍率时复用已有空间，返回 1× 时释放；10× 的双屏双缓冲加 3D 显示层约 94 MiB，光栅器的颜色、属性和深度目标另需约 94 MiB（按像素估算，不含对齐等开销）。10× 的像素数量是 6× 的 2.78 倍；尚未测量 Switch 实机帧率。
- 原计算渲染器仅生成 1× 的 DS 原生图像，供游戏捕获使用。遇到目前不支持的阴影、线框、位图清屏或横向滚动等效果，该帧显示原生画面。
- 看实机表现可比较同一存档和频率下的 `/GBAStation/log/NdsDekoStub.log`：`fps`、`run` 是运行指标；`3D: target=...` 的 `native` 为原生兼容帧数。

## 验证

```sh
bash switchbuild.sh -j 8 --build-dir build_fulldex
c++ -std=c++17 -O2 -Isrc tests/fulldex_presentation.cpp src/fulldex/Frame.cpp src/fulldex/Presenter.cpp -o /tmp/fulldex-test && /tmp/fulldex-test
# Apple Silicon 主机：测试 JIT 的代码范围和被改写字面量的失效，隔离操作系统页面保护。
c++ -std=gnu++17 -O2 -DJIT_ENABLED -Isrc -Wl,-dead_strip tests/jit_invalidation.cpp -o /tmp/jit-test && /tmp/jit-test
```

显示测试以记录绘制命令的方式检查几何和时序；实际像素、GPU 同步和性能需在 Switch 上验证。移植来源为 Pokémon Fulldex 的 `melonDS/src/GPU3D_WideGpu*` 与 `wide_gpu/` 着色器，遵循项目 GPL-3.0-or-later 许可。
