# 02: 提权 + ETW 进程速率

**What to build:** 清单用 `asInvoker`（不能用 `requireAdministrator`，否则工单 04 的受限模式没有入口）。启动后检测是否提权：已提权则建立 `Microsoft-Windows-Kernel-Network` 实时 ETW 会话，按 PID 累计事件 10/11（TCP 收/发）与 26/27（UDP 收/发），解析源/目的地址区分回环与外网，按归属规则 C 聚合，在窗口下方显示 Top 5：exe 名 + 速率条 + MB/s。未提权则显示一行提示，不启动 ETW。

**Blocked by:** 01

**Status:** done

- [x] 非管理员建内核 provider 会话的失败已实测：`logman` 返回“拒绝访问”
- [x] ETW spike 验证事件可取 PID 级字节数（9 个 PID，含 msedge 5.86 MB）
- [x] 已提权时 Top 5 出现，浏览器播放 1080p 时浏览器进程进 Top 3
      （`etw-verify10.csv` 里 msedge.exe 连续 9 秒排第一，1.0–2.6 MB/s）
- [x] ETW 按进程字节与独立载荷计数误差 <5%
      （`etw-verify11.csv`：`load_etw=12007555` vs `payload=12000000`，`+0.06%`，`etw_lost=0`）
- [x] 本地代理（verge-mihomo）不计入 Top 列表，回环流量归发起方
      （提权截图：msedge.exe 3.1M 第一，verge-mihomo 不在列表）
- [x] 程序退出后不残留会话
      **这条之前的结论是错的**：`logman query -ets` 在非提权 shell 里看不到内核会话，
      当时查到的空表不能证明没有残留。提权查询可见 5 个 `NetSpeedMonitor-<pid>` 残留会话
      （强杀实例留下的）。现在正常退出走 `Dispose → Stop`，异常残留由启动时的
      `StopStaleSessions()` 清理，会话名固定为 `NetSpeedMonitor`。
- [x] 未提权启动时不启动 ETW，界面给出明确提示
      （截图 `nsm-02-restricted.png`：黄色条“未提权，进程速率不可用”）

## 对账方法与结论

独立参照物不能用 `GetIfTable2`：网卡计数是 L2 线缆字节（含 802.11 帧头、FCS、加密开销、重传），
ETW 计数是 TCP/UDP 载荷，两者天然有系统性偏差。实测同一窗口 `wire/payload = 1.0606`。

改用「载荷对载荷」：探针下载固定 12 MB 区间，curl 用 `-w "%{size_download}"` 报出实际载荷，
与 ETW 记在该 PID 上的外网字节直接比。

| 项 | 值 |
|---|---|
| curl 载荷 | 12,000,000 B |
| ETW 记在 curl PID 上的外网字节 | 12,007,555 B |
| 偏差 | +0.06% |
| ETW 丢事件 | 0 |

两个必要前提，缺一条就测不准：

- 实时会话缓冲要放大（`BufferSizeMB=256`、`BufferQuantumKB=1024`）。默认缓冲在 2 MB/s 下
  丢约 12% 事件，且丢事件时 `EventsLost` 仍可能报 0（丢在内核侧）。
- 最终快照要在 curl 退出后再等 1.2 秒，让 pump 线程排空尾部事件。不等会漏最后几 MB
  （实测 `-4.46%`）。

## 未解决：汇总口径的 15% 缺口

同一窗口「ETW 外网合计 vs `GetIfTable2`」仍有 -11% ~ -16% 的差，`etw_lost=0` 排除了丢事件：

| 场景 | adapter_in | etw_in | diff_in_pct |
|---|---|---|---|
| 空窗 15s，msedge 后台 1.5 MB/s | 24,104,485 | 20,366,218 | -15.51 |
| 带 12MB 载荷 | 33,495,580 | 28,715,616 | -14.27 |

同一个 NIC 在安静窗口直测 `wire/payload = 1.0606`，解释不了 15%。候选原因：Wi-Fi 驱动把
MAC 层重传帧也计进 `ReceivedBytes`，但只有一份交给 IP 栈。未验证。

影响面：界面底部「ETW 外网 X/s」会比上方网卡速率低 10–15%，同一窗口两个数字不一致。
工单 03 处理（改文案或改口径）。
