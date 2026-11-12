# 03: 聚合与稳定排序

**What to build:** Top 列表按 3 秒滑动平均排序，数字仍显示 1 秒瞬时值。同一 exe 的多个 PID 合并为一行，速率为各 PID 之和。PID 复用按进程启动时间区分。进程退出后随滑窗衰减，3 秒内移出列表（上限 5 秒）。

**Blocked by:** 02

**Status:** done

- [x] 同一 exe 开 3 个实例只占一行，速率为三者之和
      （`ProcessAggregatorTests.MergesPidsOfTheSameExeIntoOneRow`：两个 PID 1M+3M → 一行 4M）
- [x] 持续下载时 Top 1 不因 1 秒抖动频繁换位
      （`SortsByThreeSecondAverageNotByInstantaneousValue`：瞬时 1.2M 的进程排在稳定 500K 的后面）
- [x] 结束一个进程后 5 秒内从列表消失，期间不出现速率突变
      （`RowDisappearsWithinFiveSecondsAfterLastByte`：停流后首帧显示 0，第 3 个空闲帧移出）
- [x] 单元测试覆盖：exe 合并、3 秒滑动平均、PID 复用判定
      （`tests/NetSpeedMonitor.Tests/ProcessAggregatorTests.cs`，5 个用例全绿）
- [x] 底部「ETW 外网 X/s」与上方网卡速率不同口径（载荷 vs L2 字节），实测低 10–15%，
      同一窗口两个数字并排会显得矛盾：改成不显示速率，或标注为「载荷」口径
      （改成「ETW 载荷 X/s」，并实测同窗口 2.3M/s vs 网卡 2.46MB/s，差 6.5%）

## 实现要点

- `ProcessAggregator` 取代 `ProcessRates.Compute` 供 UI 使用；`Compute` 保留给无头探针。
  两者共用 `ProcessRates.Delta` / `IsLocalProxy`，代理阈值只有一处定义。
- 合并发生在同一 tick 内：先按键累加，再写回条目。直接赋值会让后一个 PID 覆盖前一个。
- PID 复用：每 tick 比对 `ProcessIdentity.StartTicks`，变了就丢弃该 tick 的增量，
  避免新进程继承旧进程的字节数（`ReusedPidDoesNotInheritThePreviousProcessBytes`）。
- 均值低于 1 KB/s 的行不显示。不加这条时，5 行里会有 4 行是 `0 0` 的 keepalive 噪声。
- `ProcessIdentity` 在 UI 侧缓存 5 秒，避免每秒对全部进程开句柄。
