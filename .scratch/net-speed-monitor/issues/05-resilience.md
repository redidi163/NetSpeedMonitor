# 05: 健壮性

**What to build:** 每 5 秒重查默认路由并自动跟随；系统唤醒后销毁并重建 ETW 会话；断网时速率归零且不弹错；接口下拉可手动切换统计口径。

**Blocked by:** 02

**Status:** done

> 2026-11-13 注：接口下拉已取消（`SPEC.md` 定案 4），下面第 4 条判据与「实现」里的下拉部分不再适用于原生版；
> 其余三条是 `native-04` 的基线。原生版验收见 `native-04-resilience.md`。

- [x] 切换网络后 5 秒内统计口径切到新默认接口
      （`nsm-05-unplug3.txt`：22:12:31 禁用 WLAN，接口 3.7 s 变 Disabled，窗口 4.2 s 切到
      「自动 · 未连接」；恢复后接口 Up，窗口 3.1 s 切回「自动 · WLAN 2」——两次都在 5 s 内）
- [x] 睡眠唤醒后进程速率恢复更新，无需重启程序
      （用 `PostMessage(WM_POWERBROADCAST, PBT_APMRESUMEAUTOMATIC)` 走真实消息路径，
      不必真的挂起机器：`sessions_before=1`、`sessions_after=1`，无残留会话；
      t=1.25 s 列表清空，t=5.25 s curl 2.8M/s 恢复）
- [x] 断网时总速率与进程速率归零，窗口不弹错误框
      （`nsm-05-unplug3-offline-settled.png`：0.00 / 0.00、进程列表为空、
      页脚「断网 · 等待默认路由恢复」，无对话框）
- [x] 手动切换接口后总量立即按新接口计算
      （UIA 选「以太网（未连接）」：`settings.json` → `{"AdapterIndex":36}`，
      总量 1.50 → 0.00 MB/s；选回「自动」→ `{"AdapterIndex":0}`，速率恢复）

## 实现

- `AdapterSampler`：`Select(uint index)` 切接口并清基线；`FollowDefaultRoute()` 每 5 秒重查
  `GetBestInterface(0)`；`ResetBaseline()` 供唤醒使用。`SelectDefaultAdapter()` 保留为别名
  （探针在用）。
- `AdapterCatalog`：枚举非 loopback/tunnel 接口，Up 优先；`NameOf(index)` 供下拉和标签复用。
- `Settings`：只存 `AdapterIndex`（0=自动），`%LOCALAPPDATA%\NetSpeedMonitor\settings.json`；
  `Load`/`Save` 都不抛异常，文件损坏回退默认值（3 个单测）。
- 下拉框在窗口第 0 行，`ComboBoxItem.Tag` 存接口索引；`AutomationProperties.Name` 写
  「统计接口：…」便于 UIA 读取（ContentPresenter 不是 TextBlock，否则自动化树里读不到）。
- 跟随逻辑放在 `Tick` 里每 5 秒执行；下拉打开时跳过重建，避免吞掉用户点击。
- 唤醒走窗口自己的 `WndProc`（`WM_POWERBROADCAST` 的 `PBT_APMRESUMEAUTOMATIC` /
  `PBT_APMRESUMESUSPEND`），而不是 `SystemEvents`：SystemEvents 的监听窗口是 message-only，
  无法在不真挂起机器的情况下验证这条路径。3 秒内重复的 Resume 只重建一次。
- 断网判定用 `_sampler.Index == 0`（无默认路由）。进入/离开断网各 `_aggregator.Reset()` 一次，
  避免把断网区间的累计差值算进恢复后的第一帧；断网期间不更新 Top。
- 下拉里固定的接口被拔掉时追加「接口 N（已移除）」并保持选中，不静默回落自动。

## 验证脚本

`%TEMP%\nsm-05-unplug3.ps1`（断网/恢复计时 + 截图）、`nsm-05-resume.ps1`（模拟唤醒消息）、
`nsm-05-restore.ps1`（断网测试的兜底：45 秒后强制启用网卡）。都是提权运行，日志同目录 `.txt`。
