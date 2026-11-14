# native-08: 任务栏读数（网速 / 内存 / CPU）

**What to build:** 在任务栏右侧常显一行 `↑0.12 ↓1.20 MB/s · 内存 42% · CPU 12%`。复用现有 `AdapterSampler` 的网速，新增 `GetSystemTimes` 差值算 CPU、`GlobalMemoryStatusEx().dwMemoryLoad` 算内存。不做配置项、右键菜单、拖拽、多显示器、插件、状态条。

**Blocked by:** native-07

**Status:** done

## 结论

被测二进制：`native\bin\NetSpeedMonitor.exe`，283,648 B。

| # | 判据 | 门槛 | 实测 | 结论 |
|---|---|---|---|---|
| 1 | 位置 | 右侧间隙 ≥0 且 ≤12 px，垂直偏差 ≤2 px | 间隙 9 px，垂直 0 px | 通过 |
| 2 | CPU 对照 | ≤3 个百分点 | 6 轮：2 / 1 / 1 / 3 / 1 / 1 | 通过 |
| 3 | 内存对照 | ±1 个百分点 | 6 轮：1 / 1 / 1 / 0 / 0 / 0 | 通过 |
| 4 | 与球同源 | 同一 tick 数值一致 | 球 `0.04 / 1.54` = 读数 `↑0.04 ↓1.54` | 通过 |
| 5 | 不抢焦点 | 前台窗口不变 | `foreground_before=460436 after=460436` | 通过 |
| 6 | 全屏隐藏 | 全屏时不可见，退出后恢复 | `visible=False` → `True`，2.5 s 内 | 通过 |
| 7 | 体积/内存 | exe <320 KB，private 增量 <1 MB | 283,648 B，6.21–6.45 MB | 通过 |

## 实现

`native/src/taskbar.cpp` / `taskbar.h`，`TaskbarWindow::Create()` 建窗口，`Update(down, up)` 挂在 `main.cpp` 的 1 s `OnTick` 上。

- 窗口：`WS_POPUP` + `WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT`，点击穿透、不进 Alt+Tab、不抢焦点。
- 位置：右边缘 = `TrayNotifyWnd.left − 6 DIP`，垂直居中；宽度每 tick 按文本 `GetTextExtentPoint32W` 重算（同 TrafficMonitor 的 `AdjustTaskbarWndPos`）。
- 绘制：GDI 双缓冲 `DrawTextW`，Segoe UI 11 pt 按任务栏 DPI 缩放（144 dpi → 42 px 高、18 px 字号）；浅色 `#F3F3F3` / `#1A1A1A`，深色反过来，主题启动读一次 `AppsUseLightTheme`。
- 隐藏：前台窗口矩形覆盖任务栏所在显示器（±2 px，排除 `Progman` / `WorkerW` / 任务栏自身），或任务栏不在其显示器内。

## 关键实测：`SetParent` 在 Win11 26200 上不可用

TrafficMonitor 的 `TaskBarDlg.cpp:1017` 用 `SetParent(m_hWnd, Shell_TrayWnd)` 挂进任务栏。照做后：

```
child 47059312 NSM.Taskbar pid=60104 visible=True
dock: setparent prev=000000000001000C err=0
```

句柄、父窗口、`IsWindowVisible` 全部正常，但屏幕上没有任何像素。为排除“画错了”这一可能，把底色临时改成纯红 `RGB(255,0,0)` 再截图取点：

| 形态 | 取点 `(400,36)` | 结论 |
|---|---|---|
| `SetParent` 子窗口 | `246,223,245`（任务栏亚克力下的壁纸色） | 未被合成 |
| 顶层悬浮 | `255,0,0`（红底）/ `26,26,26`（字） | 正常显示 |

同一份绘制代码、同一位置，只有父子关系不同，所以结论是任务栏的 DirectComposition 表面盖住了子窗口。`SPEC.md` 架构一节已记录，避免后来者按原版重做一遍。

## 判据与证据

日志：`%TEMP%\opencode\nsm-08-a.txt`（位置）、`nsm-08-b.txt`（数值 6 轮）、`nsm-08-c.txt`（字号 / 全屏）。

```
before: ours=2897,2103 412x42 visible=True
before: right_gap=9 vcenter=0
fullscreen: ours_visible=False (expect False)
restored: ours_visible=True (expect True) rect=2897,2103 412x42
```

数值对照取「先采 `\Processor(_Total)\% Processor Time` 1 s 样本、紧接着截图」，两窗口覆盖同一秒：

| 轮次 | 参照 CPU | 读数 CPU | 差 | 参照内存 | 读数内存 |
|---|---|---|---|---|---|
| idle-1 | 35 | 37 | 2 | 94 | 93 |
| idle-2 | 37 | 38 | 1 | 94 | 93 |
| idle-3 | 42 | 43 | 1 | 94 | 93 |
| load-1 | 41 | 44 | 3 | 94 | 93 |
| load-2 | 42 | 43 | 1 | 94 | 94 |
| load-3 | 46 | 45 | 1 | 94 | 94 |

首轮“先截图后采样”的对照差到 7 个点（`ref_cpu=33` vs 读数 40），是采样窗口错开一秒，不是算法偏差；改成同窗口后最大 3 个点。

截图：`shots/native-08-taskbar.png`（旧 9 pt）、`native-08-font.png`（11 pt）、`native-08-fullscreen.png`（纯黑，无读数）、`native-08-restored.png`。

## 已知边界

- 可见性判断走 1 s tick，进入全屏后最多 1 s 内仍可能看到读数。
- 只处理任务栏在显示器底部的布局；任务栏竖放（左/右）时位置算法不适用。
- 主题只在启动时读一次，运行中切换浅/深色不跟随。
- 自动隐藏的任务栏是滑出屏幕而不是 `SW_HIDE`，所以用“任务栏矩形是否完整落在显示器内”判定，不用 `IsWindowVisible`。
