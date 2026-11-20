# NetSpeedMonitor

Windows 任务栏网速读数 + 悬浮球 + 进程级 Top 5 面板。单个 exe，C++20 / Win32，不依赖 .NET 或 VC 运行时。

![任务栏读数](docs/taskbar.png)

## 功能

- **任务栏读数**：贴在任务栏托盘区左侧，常显一行 `↑ 41 KB/s ↓  1 MB/s · 内存 42% · CPU 12%`，每秒刷新。速率数字固定3位宽度右对齐，百十位为空则不显示，无小数；单位随速率自动在 KB/s/MB/s 切换。右侧常显 2.4G 鼠标电量胶囊（A8A5:2255，Viper V3 Pro / K-snake 系列）：无设备时整段隐藏，有设备但未读到时显示 `--` 灰底，正常时显示百分比（充电时加 `+` 后缀），底色随电量由绿过渡到红，≤20% 放电时 500 ms 闪烁。
- **悬浮球**：76×76 圆形小窗，球上直接显示上下行速率。
- **Top 5 面板**：单击悬浮球展开，280×348，显示默认路由网卡总量、60 秒下行走势和占用带宽最大的 5 个进程；点面板外任意位置收起。面板可拖动，拖完悬浮球跟到面板左上角。
- **托盘**：左键单击显示/隐藏悬浮球；右键菜单有显示/隐藏悬浮球、显示/隐藏面板、退出。
- **流量统计**：按天累计每个 exe 的载荷字节，面板底部一行显示今日合计，点「明细 ›」打开排行窗口（今日 / 近 7 天，Top 10）。数据落在 `%LOCALAPPDATA%\NetSpeedMonitor\usage\YYYY-MM.tsv`，保留 90 天。
- 前台窗口全屏覆盖显示器时，任务栏读数与悬浮球一起隐藏，退出全屏后恢复。
- 断网时总量归零、清空 Top 列表，恢复后重新起算。

任务栏读数用的是顶层悬浮窗口，不是 `Shell_TrayWnd` 的子窗口：在本机 Win11 26200 上 `SetParent` 返回成功但不被合成，子窗口不会真正画出来。这条实测结论记在 `SPEC.md`。

## 系统要求

- Windows 10 及以上（本机验证环境：Windows 11 build 26200，144 dpi，任务栏在屏幕底部）。
- **管理员权限**：进程级字节数来自 ETW `Microsoft-Windows-Kernel-Network`，非管理员建不了实时会话。exe 内嵌 manifest `requireAdministrator`，启动必然弹 UAC，拒绝则不启动。

## 构建

需要 Visual Studio 2022 或更新版本，含 MSVC C++ 工作负载（C++20 编译器），不需要 CMake：

```bat
native\build.bat
```

产物是 `native\bin\NetSpeedMonitor.exe`（约 387 KB，`/MT` 静态链接）。

`build.bat` 第 5 行写死了 `vcvars64.bat` 的路径，本机是 VS 2026 Professional
（`%ProgramFiles%\Microsoft Visual Studio\18\Professional\...`，MSVC 14.51）。装 VS 2022 的话目录名是
`2022` 而不是 `18`，改这一行即可。

## 运行

```text
NetSpeedMonitor.exe             正常启动：托盘图标 + 悬浮球 + 任务栏读数
NetSpeedMonitor.exe --panel     启动后直接展开面板（截图、排查用）
NetSpeedMonitor.exe --probe 30  不开界面，打印网卡累计字节 30 秒
NetSpeedMonitor.exe --battery-probe N  不开界面，探测鼠标电量 N 轮，每条样本一行
```

`--probe` 输出 `T <秒> <累计下行字节> <累计上行字节>`，用来和 `Get-NetAdapterStatistics` 对账。另有 `--etw-probe`、`--etw-dump`、`--usage-probe` 三个诊断入口，用法见 `SPEC.md`。

单实例由 `Local\NetSpeedMonitor.Native` 互斥量保证，重复启动直接退出。

## 已知限制

- 只统计默认路由接口，每 5 秒重查一次。不做多网卡合计，也没有接口下拉框。
- 单位固定 KB/s（1 位小数），没有 Mbps / MB/s 切换。
- 不写任何配置：窗口位置、悬浮球显隐都只在本次运行内有效，没有开机自启。
- 流量统计只记程序运行期间，退出后的流量不计；关机或程序没启动的时段是空白，不是 0。
  数据按天分行、保留 90 天，跨月的旧文件在启动时整月删除。进程按 exe 名合并，不显示路径。
- 速率口径是 TCP/UDP 载荷，比网卡层的线缆字节低约 6%（少掉帧头、加密与重传开销）。
- 走系统代理上网时，回环流量归发起方进程，代理进程本身不计入 Top 列表，避免同一份字节算两次。规则写在 `SPEC.md` 的「归属规则」。

## 目录

| 路径 | 内容 |
| --- | --- |
| `native/src/` | 交付物源码（C++/Win32） |
| `src/NetSpeedMonitor/`、`tests/`、`tools/` | C#/WPF 版行为基准，仅作历史参考，不再发布 |
| `SPEC.md` | 规格、定案与验收判据 |
| `.scratch/net-speed-monitor/issues/` | 逐条工单（含踩坑记录） |

## 许可

MIT，见 [LICENSE](LICENSE)。
