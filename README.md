# Win95-3Dmaze-Remake

**Win95 3D迷宫重制版**：用 C++20、Win32 和 Direct3D 11 重写经典 3D 迷宫屏幕保护程序。保留自动生成迷宫、右手法则漫游、墙体升降、笑脸终点、游荡老鼠、多面体翻转等熟悉的元素，并加入高清材质、现代屏幕适配、小地图和速度调节。

> 本项目由**月半菌**制作，是非官方重制作品，不隶属于 Microsoft。项目主页：[github.com/ElliottZhao666666/win95-3dmaze-remake](https://github.com/ElliottZhao666666/win95-3dmaze-remake) · [www.ybjun.com](https://www.ybjun.com)

## 功能亮点

- 单个 64 位 `.scr` 文件：材质、着色器与设置窗体均嵌入程序资源，不需要额外的贴图文件。
- 迷宫采用深度优先回溯生成，并优先延伸直走廊，减少密集短岔路；角色按右手法则自动巡航。
- D3D11 渲染支持动态宽高比、65° 垂直视角、V-Sync、纹理过滤和轻量环境遮蔽。
- 保留经典墙体升降、茶壶画框、旋转多面体、老鼠、起点牌和终点笑脸。
- `dx.jpg` 与 `yb.jpg` 是额外墙面彩蛋：每张在每座迷宫中独立随机决定是否出现，最多出现一次，因此可能一张也没有、只有一张或两张都有。
- 小地图显示蓝色终点圆点与浅蓝色朝向箭头；迷宫大小、质量、速度、小地图等选项保存在当前用户注册表。
- 设置界面由可编辑的 Win32 `.rc` 对话框资源定义，并启用 Windows Common Controls v6 视觉样式。

## 使用方法

从构建产物中取得 `Win95-3Dmaze-Remake.scr`。直接双击会打开设置窗口；安装为系统屏保时，把文件复制到 Windows 的屏保目录，或通过 Windows 的屏保安装方式选择该文件。请仅安装自己编译或信任来源的 `.scr`，它在技术上是可执行程序。

| 命令 | 功能 |
| --- | --- |
| `Win95-3Dmaze-Remake.scr /c` | 打开设置窗口；不带参数也会进入设置。 |
| `Win95-3Dmaze-Remake.scr /s` | 全屏运行屏保。移动鼠标超过阈值、按键或点击后退出。 |
| `Win95-3Dmaze-Remake.scr /p <HWND>` | 在系统提供的父窗口句柄中渲染预览。 |

`build/Release/` 中还会生成 `run_c.bat`、`run_s.bat` 和 `run_p.bat`。最后一个脚本会启动本地预览宿主，不必手动查找 HWND。

设置保存在 `HKEY_CURRENT_USER\Software\Microsoft\Screensavers\3DMaze`：`MazeSize`（8–32）、`Speed`（1–10）、`Quality`（0–2）、`ShowMap` 和 `FullScreen`。初始值分别为 16、3、2、1、1。

## 技术栈

| 领域 | 实现 |
| --- | --- |
| 应用与设置 | C++20、Win32 API、`.rc` 对话框、Common Controls v6、注册表 |
| 图形 | Direct3D 11、DXGI、HLSL Shader Model 5、WIC 图片解码 |
| 构建 | CMake、MSVC x64、Windows SDK；静态 C/C++ 运行时 `/MT` |
| 资源 | PNG/JPEG 图片及预编译着色器通过 `RCDATA` 内嵌 |
| 测试 | C++ 迷宫逻辑测试、Python PE 资源校验 |

## 开发与编译

需要 Windows、Visual Studio 2026 **Build Tools**（含 MSVC x64 与 Windows 10/11 SDK）和 CMake；不需要安装完整 Visual Studio IDE。可选安装 Python 3 以运行资源校验脚本。

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release --target 3dmaze maze_logic_tests --parallel 1
.\build\Release\maze_logic_tests.exe
python tests\verify_resources.py build\Release\Win95-3Dmaze-Remake.scr
```

成品位于 `build/Release/Win95-3Dmaze-Remake.scr`。若 VS Build Tools 已配置其他版本，请将生成器名称改为本机 `cmake -G` 列出的对应项。源码与素材更新后重新构建即可；`build/` 不纳入 Git。

设置窗体的布局、标题和静态文字集中在 [`Resource.rc`](Resource.rc)，控件 ID 在 [`resource.h`](resource.h)，交互与注册表代码在 [`src/main.cpp`](src/main.cpp)。可使用支持 Win32 对话框资源的可视化编辑器调整 `.rc` 布局，之后仍通过上述 CMake 命令编译。

项目目录：`src/` 为原生程序，`shaders/` 为 HLSL，`assets/` 为贴图，`tools/` 为转换及调试脚本，`tests/` 为离线校验。更细的原逻辑对应说明见 [`README_NATIVE.md`](README_NATIVE.md)。

## 素材与致谢

迷宫漫游机制参考 [ibid-11962/Windows-95-3D-Maze-Screensaver](https://github.com/ibid-11962/Windows-95-3D-Maze-Screensaver) 的 `maze.js`，原始 WebGL 参考目录仅供本地开发对照，不随此仓库提交。高清图片素材由月半菌整理与制作；`dx.jpg`、`yb.jpg` 为额外彩蛋。感谢经典 Windows 95 3D 迷宫带来的灵感。

## 许可

本项目原创代码按 [`LICENSE`](LICENSE) 中的 MIT 条款开放；`assets/` 图片不属于 MIT 授权范围，具体见 [`assets/RIGHTS.md`](assets/RIGHTS.md)。原始参考项目和 Windows 95 原版屏保亦不在本项目的授权范围内。请保留出处与署名，并在复用素材前确认相应授权。

**制作：月半菌 · 2026 年 9 月 · [www.ybjun.com](https://www.ybjun.com)**
