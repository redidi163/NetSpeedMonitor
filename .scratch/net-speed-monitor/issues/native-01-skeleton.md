# native-01: C++ 骨架 + 总量采样

**What to build:** 与 C# 版并存的 Win32 骨架：单实例、托盘图标、76×76 DIP 分层窗口悬浮球、`GetIfEntry` 总量采样、提权检测。产物 `native/bin/NetSpeedMonitor.exe`，`/MT` 静态链接，无运行时依赖。

**Blocked by:** None

**Status:** done

**隔离约定（保证两版可同时运行）**

- 单实例互斥体 `Local\NetSpeedMonitor.Native`，与 C# 版不同名
- ETW 会话名 `NetSpeedMonitorNative`（02 才启用）
- 源码 `native/`，C# 版原样保留作行为基准

## 验收记录（2026-11-13）

| 判据 | 实测 | 结论 |
|---|---|---|
| 单 exe 体积 | 192512 B（188 KB） | 通过（目标 300–500 KB 以内） |
| private ≤ 6 MB | 非提权 2.86 MB / 提权 2.80 MB | 通过 |
| WS / handles / threads | 17.11 MB / 180 / 5 | 记录值 |
| 总量对账 <3% | 40 s 累计：下行 47844 B vs `Get-NetAdapterStatistics` 48692 B（1.74%）；上行 32012 vs 32155（0.44%） | 通过 |
| 球尺寸 | `GetWindowRect` = 114×114 @ dpi 144（76×144/96） | 通过 |
| 球渲染 | `.scratch/net-speed-monitor/shots/native-01-ball.png`：上 ↑ 橙 `#FF9E6D`、中 `MB/s` 灰、下 ↓ 蓝 `#4CC2FF` | 通过 |
| 单实例 | 第二个进程 2 s 内退出，exit code 0 | 通过 |
| 托盘 | 溢出区 UIA 按钮名 `NetSpeedMonitor · WLAN 2（受限）`（非提权）；提权后为 `NetSpeedMonitor · WLAN 2` | 通过 |
| 提权检测 | 非提权附加「（受限）」，提权不附加 | 通过 |

## 实现要点

- `build.bat`：`vcvars64.bat` 之后不能判 `errorlevel`——该脚本成功时也留 `errorlevel=1`，改为 `where cl` 探测。已确认 `crlf=26 lf_only=0 non_ascii=0`。
- `common.h`：`WIN32_LEAN_AND_MEAN` 下必须显式包含 `winsock2.h`（`IP_ADAPTER_ADDRESSES` / `AF_UNSPEC`）、`objidl.h`（gdiplus 的 `IStream` / `PROPID`）、`shellapi.h`（`NOTIFYICONDATAW` / `CommandLineToArgvW`）。
- `GetBestInterface` 第二参是 `PDWORD`（`unsigned long*`），不能传 `uint32_t*`。
- GDI+ 全局函数在 `Gdiplus` 命名空间，`main.cpp` 需写全 `Gdiplus::GdiplusStartup`。
- `AdapterList` 持有 `GetAdaptersAddresses` 缓冲区，避免拷贝 `IP_ADAPTER_ADDRESSES` 后 `FriendlyName` 悬空。

## 已知遗留（进 02）

- 面板未实现（球单击只切换显示/隐藏）
- Top 5 未实现，托盘菜单只有「显示/隐藏悬浮球」「退出」
- `settings.json` 只读不写，`AdapterIndex != 0` 时固定接口但不校验存活
