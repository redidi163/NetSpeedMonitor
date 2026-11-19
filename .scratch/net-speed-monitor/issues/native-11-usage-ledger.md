# native-11: 流量累计账本（内存）

**What to build:** 把每秒的 ETW 增量累加到「每个 exe 一条」的长周期账本，账本不随滑窗、进程退出、ETW 会话重建而清零。本单不落盘、不动 UI，只提供可诊断的累计值。

**Blocked by:** native-10

**Status:** done

> 判据 3/4/5（ETW 同窗口对账、睡眠唤醒、真实代理）2026-11-19 决定集中到 native-14 一次提权跑完，
> 本单以合成数据验证账本逻辑后结单。见文末「实测」。

## 口径

- 累计的是 `delta.in` / `delta.out`（回环段 + 外网段的载荷字节），与面板 Top 5 同一份增量，因此沿用同一条归属规则：
  同一窗口内回环与外网都超过 `ProxyThresholdBytes` 的进程判为本地代理，其增量不计入账本。
- 键是 `identity.key`（小写完整映像路径），显示名是 exe 名：同一 exe 多实例合并，同名不同路径分开。
- 账本自启动起累加，重启归零（落盘在 native-12）。

## 为什么不能直接读 `EtwCounters`

`counters_` 是每个 ETW 会话的累计值：`main.cpp` 在睡眠唤醒时 `etw.Stop()` + `etw.Start()`，整张表归零；
`ProcessAggregator` 的 `Entry` 在进程最后一字节之后 5 s 就被删除。两者都不能当总账，只能每 tick 取增量自己加。

## 改动

- `aggregate.h/.cpp`：新增 `UsageDelta{key,name,down,up}`；`Update()` 在归属判定之后把本 tick 的增量
  记入 `usageDeltas_`，通过 `UsageDeltas()` 暴露；`Prime()` 路径清空。
- `usage.h/.cpp`：`UsageLedger::Add()` 按键累加，`Top(n)` 按 `down+up` 降序，`TotalDown/TotalUp` 单调；
  `FormatBytes()` 输出 `1.2 GB` / `340.5 MB` / `12.3 KB` / `0 B`。
- `main.cpp`：`App` 增加 `UsageLedger usage`，`OnTick` 在 `aggregator.Update()` 之后送增量；
  新增 `--usage-probe <秒> <文件>` 诊断入口（与 `--etw-probe` 同样写文件，因为提权进程拿不到 stdout）。
- `build.bat`：编译单元加 `src\usage.cpp`。

## 判定依据

| # | 判据 | 方法 |
|---|---|---|
| 1 | 累计单调不减 | `--usage-probe` 每秒的 `T` 行，`TotalDown/TotalUp` 不出现下降 |
| 2 | 相加守恒 | 每 tick 的 `TotalDown` 增量 = 该 tick 各 `U` 行之和（账本无丢行） |
| 3 | 与 `--etw-probe` 同窗口对账误差 <5% | 两个进程同时各跑 30 s，比较累计外网字节 |
| 4 | 睡眠唤醒不清零 | 唤醒后首个 `T` 行长值 ≥ 唤醒前最后一个值 |
| 5 | 代理不重复计入 | 走系统代理下载时，`verge-mihomo` 不出现在 `U` 行 |

判据 3/4/5 需要提权会话（ETW 与 `requireAdministrator` manifest）。

## 实测（2026-11-19）

构建：`native\build.bat` 通过，`native\bin\NetSpeedMonitor.exe` 300,032 B（加 `usage.cpp` 前 284,672 B）。

本会话 `SessionId=6` 没有提权通道（`Start-Process -Verb RunAs` 与直接启动 `requireAdministrator` 的 exe 都返回
`ERROR_NOT_SUPPORTED`，`consent.exe` 计数 0），所以判据 3/4/5 未跑。判据 1/2 与归属规则改用合成数据验证：
把 `aggregate.cpp` / `usage.cpp` / 头文件复制到 `%TEMP%\opencode\nsm11\`，用 `cl` 单独编一个控制台 harness，
喂 10 个 tick、每 tick `externalIn +100000` / `externalOut +20000`。

| # | 判据 | 实测 | 结论 |
|---|---|---|---|
| 1 | 累计单调不减 | 10 个 tick 的 `totalDown` 依次 0→100000→…→900000 | 通过 |
| 2 | 相加守恒 | `Top(32)` 各行之和 = `TotalDown+TotalUp` = 1,080,000 | 通过 |
| 3 | 与 `--etw-probe` 对账 | 未跑（需提权） | 阻塞 |
| 4 | 唤醒不清零 | 未跑（需提权） | 阻塞 |
| 5 | 代理不重复计入 | 合成数据：loopback 与 external 同窗口各 500000 → 账本增量 0 | 通过（合成） |

附加：首个 tick 只建基线不加账（`accounts=0`）；零增量不新建账号；`FormatBytes` 四个阈值
（`0 B` / `1.2 KB` / `2.5 MB` / `1.5 GB`）全对。

harness 输出：

```
tick  1 totalDown=0 totalUp=0 accounts=0
tick 10 totalDown=900000 totalUp=180000 accounts=1
PASS totals never decrease
PASS down == 9 x 100000
PASS local proxy window dropped
PASS Top() sums to reachable totals
final totalDown=900000 totalUp=180000 accounts=1 failures=0
```

合成数据只能证明账本逻辑与归属规则的实现，不能替代真机对账：ETW 事件丢失、`GetIfEntry` 与载荷口径差异、
真实代理进程名这三件事必须等提权会话。native-12 因此不启动，直到判据 3/4/5 补齐。
