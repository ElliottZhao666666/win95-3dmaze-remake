# 原生 3D Maze 屏幕保护程序

此版本以 `Windows-95-3D-Maze-Screensaver/maze.js` 的行为为依据，用 C++20 和
Direct3D 11 重建迷宫循环。原始 WebGL 文件夹在本地保留，Git 不跟踪它。

## 构建

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release --target 3dmaze
cmake --build build --config Release --target maze_logic_tests
./build/Release/maze_logic_tests.exe
python tests/verify_resources.py build/Release/Win95-3Dmaze-Remake.scr
```

编译结果位于 `build/Release/Win95-3Dmaze-Remake.scr`。CMake 同时在这个目录生成：

- `run_c.bat`：打开设置面板；
- `run_s.bat`：全屏运行；
- `run_p.bat`：打开 640×400 的预览宿主窗口，再以 `/p <HWND>` 运行屏保。

## 与 WebGL 原逻辑的对应

`Grid::generate()` 对应 `newMaze()` 与 `resetVars()`：随机起点的迭代式深度优先回溯、
优先沿当前方向继续挖掘以减少密集短岔路、
随机起点及朝向、起点前方标牌、六个多面体、三个 OpenGL 标牌、随机终点、
随机老鼠位置和三幅随机封闭墙茶壶画框。`dx.jpg` 与 `yb.jpg` 各自独立随机出现
零或一次。`Walker::choose()` 对应 `nextMove()`：
转向后优先前进，再按右、前、左、回头的规则决策。

`World::update()` 管理墙升起、漫游、多面体碰撞后的画面翻转、抵达笑脸后的墙下沉与
下一座迷宫。原脚本的翻转最终使画面倒置 180°；这里把该过程改成 1.5 秒的
时间插值。相机默认速度为 1.5 格/秒，老鼠按原脚本的步进比例以半速移动，
转向立即完成。移动与动画都只取决于经过的时间，不取决于显示器刷新率。

`MazeRenderer` 负责双面墙、地板、天花板、随机画框、朝向相机的精灵、
四种旋转多面体和小地图。小地图用蓝色实心圆标记终点，浅蓝色三角形标记视角和朝向。
空间高度为 0.82 单位，相机始终位于地板与天花板中间；原生渲染坐标采用 Y 轴竖直。
所有纹理和预编译着色器均嵌入 `.scr` 资源区。
资源编译会跟踪 `assets/` 文件变化；替换材质后重新构建即可更新程序。
