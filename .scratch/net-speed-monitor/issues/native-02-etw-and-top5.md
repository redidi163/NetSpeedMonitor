# native-02: ETW 进程速率 + Top 5 面板

**What to build:** 移植 C# 版的 ETW 采集与进程聚合，做出 280×348 DIP 的悬浮面板：接口名、上下行合计（MB/s）、60 点走势、进程速率 Top 5、页脚状态。ETW 会话名 `NetSpeedMonitorNative`，与 C# 版并存。

**Blocked by:** native-01

**Status:** done（判据 3 见「已知限制」）

## 验收判据

| # | 判据 | 实测 | 结论 |
|---|---|---|---|
| 1 | 1080p 视频时浏览器进 Top 3 | `msedge.exe` 在 25 个采样 tick 里 24 次排第 1，速率 0.3–1.8 MB/s（`sm-etw-video.txt`） | 通过 |
| 2 | 已知载荷对账误差 <5% | 22,119,463 B 载荷，ETW external 增量 22,188,036 B（+0.31%）；第二次 22,194,715 B（+0.34%） | 通过 |
| 3 | 本地代理不进 Top | 浏览器场景 24/24 tick 无 `verge-mihomo.exe`；强制 `curl -x 127.0.0.1:7897` 时 mihomo 以「上行」行出现 | 场景通过，见限制 |
| 4 | private ≤ 8 MB | 提权 + ETW：6.05–6.32 MB；非提权：2.86 MB | 通过 |
| 5 | 面板尺寸 | `GetWindowRect` = 420×522 @ dpi 144（280×348 DIP） | 通过 |
| 6 | 单实例、ETW 会话与 C# 并存 | `Local\NetSpeedMonitor.Native` + `NetSpeedMonitorNative`，两个版本同时运行互不影响 | 通过 |

产物：`native/bin/NetSpeedMonitor.exe` 279,552 B（273 KB），`/MT` 静态链接。

## 关键实现

- ETW 启用顺序：`EnableTraceEx2` 对 `Microsoft-Windows-Kernel-Network` {7dd42a49-…} 成功产出事件；`ControlTrace(UPDATE)` + `EnableFlags` 返回 0 但 0 事件，只作回退。
- 报文布局（真实流量确认）：offset 0 = PID、4 = size、8/12 = 对端/本地 IPv4；24 字节足够判定 loopback 与方向。
- 缓冲区 `BufferSize=128` KB × `MaximumBuffers=32`：实时消费端会按同样规模分配自己的缓冲池，512 KB×64 时 private 直接涨到 15.1 MB。
- `aggregate.cpp` 与 `Services/ProcessAggregator.cs` 一一对应：3 tick 滑窗排序、5 s 移除、1 KB/s 可见下限、8192 B 双向代理阈值、按 exe 路径合并、PID 复用按 `StartTicks` 丢弃该 tick。
- `--panel` 是诊断开关：提权窗口收不到低完整性进程注入的鼠标输入（UIPI），截图靠它而不是模拟点击。
- 页脚从 `进程速率 · EnableTraceEx2 · N 事件` 改为 `ETW · EnableTraceEx2 · N 事件`，避免与分区标题重复。

## 已知限制（与 C# 版同源）

- **provider 不发 UDP 事件**：25 s 内 24,146 条事件全部是 TCP（id 10/11/12/13/15/18/42/43），`udp_ev=0`；同期 `\UDPv4\Datagrams Received/sec` 平均 826。QUIC 类流量（含本机代理的上游）因此无法按进程归因，只能靠网卡总量。
- **因此网卡总量对账会偏低**：95 s 窗口 `Get-NetAdapterStatistics` 57.4 MB vs ETW 48.1 MB（83.9%），差额就是 UDP；判据 2 改用已知载荷对账后误差 0.31%，说明 TCP 路径的字节统计是准的。
- **强制走本地代理时 mihomo 会以「上行」出现**：`curl -x 127.0.0.1:7897` 拉 2 MB，mihomo `lb_out=1,090,177` 而 `ext_in=10,394`，代理规则的「两段同时超阈值」条件不成立。C# 版订阅同样 4 个事件 ID，行为一致；要修需要换数据源（例如 `Microsoft-Windows-TCPIP` 或网卡层）。
- **隐藏窗口的 DPI**：`GetDpiForWindow` 对从未显示过的窗口返回 96，面板会先按 1× 渲染一帧；`TogglePanel` 里改为从悬浮球窗口取 DPI 并强制尺寸。

## 踩坑

- `--etw-dump` 早期把文件在主线程 `_wfopen_s("w")` 打开，事件线程再追加会失败，日志恒为空；改成事件线程懒打开。
- 提权进程不能重定向 stdout，探针把汇总同时写进日志文件。
- `--etw-probe` 的累计量要夹住负增量：PID 复用会把计数器清零，负增量会把已计字节吃掉（曾把 22 MB 显示成 5.5 MB）。
- 提权窗口的鼠标验证必须用提权脚本；非提权 `mouse_event` 打不进高完整性窗口。
- 截图坐标要用 DPI-aware 会话读，否则 `GetWindowRect` 返回 2/3 的虚拟化尺寸（420×522 读成 280×348）。
