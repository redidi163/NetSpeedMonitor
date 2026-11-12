# 04: 受限模式

**What to build:** 非管理员启动时不弹 UAC、不退出，显示总量速率（`GetIfTable2`）+ 当前连接数（`GetExtendedTcpTable`）+ Top 5 进程（仅连接数，不显示字节数）+ 提示条「以管理员身份重启可查看进程速率」和一个重启按钮。

**Blocked by:** 02

**Status:** done

- [x] 以普通权限启动，窗口正常显示且标明受限模式
      （`nsm-04-restricted.png`：黄条 + 「进程连接 · 81 条」+ 底部「受限模式 · 进程按连接数排序」）
- [x] 受限模式下不显示任何进程字节速率，也不显示伪造的 0
      （列表只有「24 连接」这种计数列，没有 MB/s 列）
- [x] 点重启按钮触发 UAC 并以管理员重启，受限提示消失
      （UIA 调 `InvokePattern` 点按钮：PID 44136 → 30640，黄条消失、出现「进程速率」，
      提权查询只剩 1 个 `NetSpeedMonitor` 会话，无残留）
- [x] 受限模式连续运行 10 分钟无异常退出
      （21:28:45 起，11 分钟存活；RSS 163.7 → 162.6 MB 无爬升；累计 CPU 12.2 s ≈ 单核 1.8%）

## 实现

- `ConnectionCounter.CountByPid()`：`GetExtendedTcpTable(TCP_TABLE_OWNER_PID_ALL)` 查 IPv4 + IPv6，
  只计 `ESTABLISHED`（state=5），按 PID 归并。不需要提权。
- `ConnectionCounter.BuildRows()`：按 exe 名合并多 PID，按连接数降序取 5 行，纯函数，有单测。
- 受限模式 2 秒刷一次连接数（查表比读计数器贵，没必要跟 1 秒刷新）。
- 列表用独立的 `ConnectionList`，与速率列表共用 Grid.Row 6，互斥显示。
- 提权路径若 ETW 建会话失败，也会落到这个分支，但页脚显示真实错误而不是「未提权」。

## 附带修复：残留 ETW 会话把内核缓冲吃光

重启按钮实测时发现新进程报 `COMException: 系统资源不足 (0x800705AA)`，两个实例都建不了会话。

根因：之前用 `Stop-Process -Force` 杀的实例，其 ETW 会话留在内核里（会话名带 PID，
`NetSpeedMonitor-51816/53520/35696/29044/24808` 共 5 个），每个按 256 MB 预留缓冲，
把池吃光后新建会话一律 `ERROR_NO_SYSTEM_RESOURCES`。

修法：

- `BufferSizeMB` 256 → 64。实测 64 MB 在 2 MB/s 下仍 `etw_lost=0`、`vs_payload_pct=+0.19%`。
- 启动时 `StopStaleSessions()`：枚举活动会话，凡是 `NetSpeedMonitor` 前缀的都 Attach 后 Stop。
  会话名改成固定的 `NetSpeedMonitor`，不再带 PID。

**同时纠正工单 02 的一条错误结论**：`logman query -ets` 在非提权 shell 里看不到内核会话，
之前据此判断「退出后无残留会话」是无效的。提权查询可见 5 个残留会话。
