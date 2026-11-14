# native-09: 单位改 KB/s，悬浮球跟随全屏隐藏

**What to build:** 所有速率读数由 MB/s 改为 KB/s（1 位小数），并把 native-08 的全屏隐藏规则扩展到悬浮球；悬浮球被全屏压住时不得改变托盘菜单的「隐藏/显示」语义。

**Blocked by:** native-08

**Status:** done

## 结论

被测二进制：`native\bin\NetSpeedMonitor.exe`，284,160 B。

| # | 判据 | 实测 | 结论 |
|---|---|---|---|
| 1 | 球显示 KB/s | `↑164.4` / `↓2454.9`，中缝标 `KB/s` | 通过 |
| 2 | 任务栏显示 KB/s | `↑164.4 ↓2454.9 KB/s · 内存 73% · CPU 24%` | 通过 |
| 3 | 球与读数同 tick 一致 | 两边都是 `164.4 / 2454.9` | 通过 |
| 4 | 面板显示 KB/s | 下行 `2416.3 KB/s`、上行 `49.7 KB/s`，单位标签未压字 | 通过 |
| 5 | 全屏隐藏球 | `ball_visible=False` | 通过 |
| 6 | 全屏隐藏读数 | `taskbar_visible=False` | 通过 |
| 7 | 退出全屏恢复 | 两者均 `True`，2.5 s 内 | 通过 |
| 8 | 托盘菜单语义 | `IsVisible()` 返回用户意图，全屏压制不改文案 | 代码路径确认 |

## 改动

- `common.cpp`：`FormatMBps` → `FormatKBps`，`%.2f` 除 1048576 → `%.1f` 除 1024。1 位小数是为球内 76 DIP 的宽度留余量：`2454.9` 在 13 px 粗体下仍不越界，`102400.00` 会。
- `common.cpp` 新增 `FullscreenOnMonitor(HWND reference)`：前台窗口矩形覆盖参照窗口所在显示器即命中，排除 `Progman` / `WorkerW` / 任务栏 / 本进程窗口，容差 ±2 px。
- `taskbar.cpp`：删掉本地 `FullscreenOnTaskbarMonitor`，改用共享函数（参照窗口 = 任务栏）。
- `ball.cpp/h`：`UpdateFullscreen(bool)` + `suppressed_` 标志，`Show()` 与压制状态分离，`ApplyVisibility()` 统一决定 `ShowWindow`；`IsVisible()` 改为返回用户意图。
- `main.cpp`：`OnTick` 里 `app.ball.UpdateFullscreen(FullscreenOnMonitor(app.ball.Hwnd()))`；走势缓冲除数同步改成 1024。
- `panel.cpp`：两处总量数字与单位标签改用 KB/s。进程行仍是 `2.4M` / `340K` 紧凑写法（`aggregate.cpp` 原有逻辑），未改。

## 证据

日志：`%TEMP%\opencode\nsm-09-a.txt`（球/读数/全屏）、`nsm-09-b.txt`（面板）。

```
ball=60,60 114x114 visible=True
taskbar=2892,2103 417x42 visible=True
fullscreen: ball_visible=False (expect False) taskbar_visible=False (expect False)
restored: ball_visible=True (expect True) taskbar_visible=True (expect True)
```

截图：`shots/native-09-ball.png`、`native-09-taskbar.png`、`native-09-panel.png`、`native-09-fullscreen.png`。

## 已知边界

- 判断走 1 s tick，进入全屏后最多 1 s 内仍可能看到读数或球。
- 球内的数字不换行、不缩字号；超过约 20 MB/s（≈20480.0 KB/s）会顶到圆边，需要时再谈换行或单位自适应。
- 面板进程行保持 `M`/`K` 紧凑单位，与总量 KB/s 不同口径，这是原设计（`aggregate.cpp` 的 `%.1fM` / `%.0fK`）。
