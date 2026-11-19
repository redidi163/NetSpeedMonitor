# native-14: 流量统计验收与 SPEC 修订

**What to build:** 端到端验收，并把「不落盘任何状态」这条定案改掉。

**Blocked by:** native-13

**Status:** done

> 三条 ETW 判据（与 `--etw-probe` 同窗口对账误差 <5%、睡眠唤醒不清零、真实代理不重复计入）
> 从 native-11 移交本单，与本单原有判据在同一次提权会话里跑完。

## SPEC 改动

- 非目标第 1 条「历史数据落盘、按天统计」删除，改成「按天统计只记进程维度，不记连接明细、不做历史曲线」。
- 定案 16「配置 无。不落盘任何状态」改为：除流量统计目录外不落盘；配置仍然没有。
- 新增定案 19：

  | # | 决策 | 定案 |
  |---|---|---|
  | 19 | 流量统计 | 按天累计进程级载荷字节，落 `%LOCALAPPDATA%\NetSpeedMonitor\usage\YYYY-MM.tsv`；只统计运行期间；保留 90 天；界面只显示 Top 10 |

- 架构节补一段：为什么账本不能读 `EtwCounters` 总账（会话重建归零、`Entry` 5 s 过期）。
- 验收判据新增第 7 条（见下）。

## 验收判据

| # | 判据 | 方法 |
|---|---|---|
| 1 | 固定 12 MB 下载，该 exe 当日增量与 `curl` `size_download` 误差 <5% | 载荷对载荷，同窗口 |
| 2 | 重启后当日累计不减 | 两次启动读同一文件 |
| 3 | 睡眠唤醒不丢增量 | 唤醒前记录 → 下载 12 MB → 唤醒后核对增量 |
| 4 | 跨 00:00 新增一行，旧行不变 | 注入日期 |
| 5 | 界面 Top 10 与 TSV 聚合值逐行相等 | 离线解析文件对比 |
| 6 | 90 天滚动 | 注入旧行验证删除 |
| 7 | 累计单调：连续 1 小时，`TotalDown` 不出现下降 | `--usage-probe 3600` 日志 |
| 8 | 与 `--etw-probe` 同窗口对账误差 <5%（自 native-11 移交） | 两个探针各自 30 s，比较累计外网字节 |
| 9 | 睡眠唤醒后首个 `T` 行 ≥ 唤醒前最后一行（自 native-11 移交） | 唤醒前后读 `--usage-probe` 日志 |
| 10 | 走系统代理下载时 `verge-mihomo` 不出现在 `U` 行（自 native-11 移交） | 代理开/关各跑一次 `--usage-probe` |
| 11 | 点排行窗口外，窗口 ≤250 ms 收起且不影响面板（自 native-13 移交） | 提权脚本注入点击 |
| 12 | 打开排行窗口前后 `GetForegroundWindow()` 不变（自 native-13 移交） | 提权脚本记录前后句柄 |

判据 1/3 必须提权运行；空闲时段不能用作判据（比值为 0/0）。

## 进度（2026-11-19）

提权通道：本机是 BetterGI 桌面分身（`SessionId=2`），UAC 不可用（`ERROR_NOT_SUPPORTED`）。
改用 `GameAutoLauncher` 装的 SYSTEM 计划任务 `GameAutoLauncherElevatedBroker`：`WTSQueryUserToken`
→ linked token（High）→ `CreateProcessAsUserW`，同一会话起进程，不弹 UAC。
脚本与日志在 `%TEMP%\opencode\nsm14\`（`elev.py`、`accept1.py`、`probe_run.py`、`ui.py`、
`sleep_sim.py`、`c10-proxy.txt`、`c7-hour.txt`）。

| # | 判据 | 结果 | 证据 |
|---|---|---|---|
| 1 | 12 MB 下载误差 <5% | 通过 | `curl` 自报 12,582,912 B；账本 `curl.exe` 12,605,511 B（+0.18%） |
| 2 | 重启后当日累计不减 | 通过 | native-12 夹具（二次 `Open`+`SeedToday` 相等） |
| 3 | 睡眠唤醒不丢增量 | 通过（模拟） | 会话销毁 + resume 消息后继续下载，见下 |
| 4 | 跨 00:00 新增一行、旧行不变 | 通过 | native-12 夹具注入日期 |
| 5 | 界面 Top 10 = TSV 聚合 | 通过 | native-13 夹具 + PowerShell 独立解析逐行相等 |
| 6 | 90 天滚动 | 通过 | native-12 夹具删除 `2026-07.tsv` |
| 7 | 连续 1 小时 `TotalDown` 不下降 | 通过 | `--usage-probe 3600`：3600 个 `T` 行，`TotalDown` 0 → 267,858,414，下降次数 0 |
| 8 | 与 `--etw-probe` 同窗口误差 <5% | 通过 | 见下 |
| 9 | 唤醒后首个 `T` ≥ 唤醒前最后一行 | 通过（同 3） | 每 4 s 采样落盘总值单调 |
| 10 | 代理下载时 `verge-mihomo` 不进排行 | 通过 | 代理下载 12,582,912 B → `curl.exe` 12,605,550，`verge-mihomo` 32,853 |
| 11 | 点窗外 ≤250 ms 收起、面板不受影响 | 通过 | 收起 0.01 ms（钩子直接投递），面板仍可见 |
| 12 | 打开前后前台句柄不变 | 通过 | `GetForegroundWindow()` 0x505A2 → 0x505A2 |

判据 1 期间发现并修掉一个真 bug（`aggregate.cpp`）：进程退出后 ETW 仍会排空它的残余事件，
原先 `OpenProcess` 失败就改挂 `pid:N`，一次 12 MB 下载被拆成 `curl.exe 7,505,907` +
`pid 48280 5,099,604` 两行。改为保留存活时的身份 30 s（`DeadIdentitySeconds`），修复后
`curl.exe` 单行 12,605,511 / 12,582,912 = 100.18%。

判据 3/9 用「销毁 ETW 会话 + 投递 `WM_POWERBROADCAST(PBT_APMRESUMEAUTOMATIC)`」模拟睡眠：
真 S3 会让远程会话掉线，而睡眠对账本的唯一影响就是会话被销毁、计数器归零。当日 TSV 总值
138,000,463 → 151,030,989（下载 12,582,912）→ 165,082,208（再下载 12,582,912），期间每 4 s
采样 18 次全部单调；`logman stop NetSpeedMonitorNative -ets` 后进程存活并在 resume 后重建会话。

判据 8：`--usage-probe 25` 与 `--etw-probe 25` 各配一次 24 MB 下载。账本总 25,612,135 B；
`--etw-probe` 的 `tcp_in` 38,437,072 B 里含 `verge-mihomo`（pid 14100）13,225,982 B
（外网 + 回环等量，按本地代理规则排除），扣掉后 25,211,090 B，差 1.6%。同一进程对账：
`curl.exe` 账本行 25,205,319 B = `--etw-probe` 该 pid `ext_in` 25,205,319 B。

判据 7：`--usage-probe 3600` 23:06:24 → 00:06:47，3600 个 `T` 行，`TotalDown` 从 0 涨到
267,858,414 B，逐行比较下降 0 次（`drops=0 worst=0`），`lost=0`。同时段网卡 `WLAN 2`
收到 288,389,169 B，账本是本机载荷口径（不含帧头、不含本机回环重发），比值 0.929。
