# native-07: 整体验收（发布目录二进制）

**What to build:** 对 `D:\githubitem\NetSpeedMonitor\NetSpeedMonitor.exe` 重跑 `SPEC.md` 的四条验收判据，外加 10 分钟常驻内存观察。不改行为，只补诊断能力与证据。

**Blocked by:** native-06

**Status:** done

## 结论

被测二进制：`D:\githubitem\NetSpeedMonitor\NetSpeedMonitor.exe`，278,016 B，
SHA256 `F3417F94B08BD3AFAA473CF4503A673C2EE9B19DB86B70D6E00284C66E1F3329`。

| # | 判据 | 门槛 | 实测 | 结论 |
|---|---|---|---|---|
| 1 | 总量对账 | <10% | 下行 0.105% / 上行 0.045% | 通过 |
| 2 | 浏览器进 Top 3 | 稳定进前三 | msedge 21/21 tick rank=1 | 通过 |
| 3 | 拒绝提权不启动 | 无窗口无残留 | `Win32Exception 740`，0 进程 | 通过 |
| 4 | 载荷对账 | <5% | 0.39% | 通过 |
| 5 | 常驻内存 | 无明显增长 | 6.0–6.6 MB，11 分钟无趋势 | 通过 |

## 诊断改动

`--etw-probe` 结束时按 PID 汇总一行，便于拿单个进程对账：

```
# pid <pid> ext_in <n> ext_out <n> lb_in <n> lb_out <n>
```

`main.cpp` 里 `RunEtwProbe` 在 `sampler.Stop()` 之后遍历 `sampler.Snapshot()` 输出。
只影响探针模式，不影响面板。

## 判据与证据

### 1. 总量对账 <10%

同一 40 s 窗口，`--probe 40` 累计 vs `Get-NetAdapterStatistics`（WLAN 2）差值：

| 方向 | 探针累计 (T39) | 网卡差值 | 误差 |
|---|---|---|---|
| 下行 | 18,306,667 | 18,325,968 | 19,301 B = **0.105%** |
| 上行 | 1,913,288 | 1,914,153 | 865 B = **0.045%** |

日志：`nsm-07-a.txt`（`stats0`/`stats1`/`stats_delta_*` 行 + `nsm-07-probe.txt` 尾部）。

### 2. 浏览器稳定进 Top 3

- 主证据：`--etw-probe 22` 期间 Edge 下载 22 MB msixbundle（ghfast），同时 curl 下载 30 MB 视频做竞争。
  `msedge.exe` 在 21 个有数据的 tick 里 **rank=1，21/21**，总 TCP 8,000,312 B；
  `curl.exe` 在 t4–t18 稳定 rank=2（t19 起 curl 退出，退化行见「已知边界」）。
  日志：`nsm-07-f.txt`、`nsm-07-etw-f.txt`。
- 旁证：`--etw-probe 25` 期间 Edge 打开 1080p 测试源，`msedge.exe` 20/20 tick 在 Top 3（19 次 rank=1）。
  日志：`nsm-07-e.txt`、`nsm-07-etw-e.txt`。

### 3. 拒绝提权不启动

中等完整性 shell 里 `[Diagnostics.Process]::Start(exe)`：

```
caught Win32Exception NativeErrorCode=740
processes_after=0
```

加载器直接拦下，没有窗口、没有残留实例。

### 4. 载荷对账 <5%

`--etw-probe 35` 期间 curl 下载 msixbundle，比 curl 自报的 `size_download`：

| 项 | 字节 |
|---|---|
| curl `size_download` | 7,666,794 |
| 文件实际长度 | 7,666,794 |
| ETW 该 PID `ext_in` | 7,696,767 |
| 误差 | 29,973 B = **0.39%** |

日志：`nsm-07-d.txt`、`nsm-07-etw-d.txt`。
旁证小样本：30 MB 视频只下到 1,064,942 B 时误差 3.76%（固定开销占比大，仅记录）。

### 5. 常驻内存（10 分钟）

发布目录 exe 冷启动后每 30 s 采样一次，共 21 点（`nsm-07-g.txt`）：

| 阶段 | private | working set | handles |
|---|---|---|---|
| t=0–240 s | 6.02–6.10 MB | 21.8–22.0 MB | 186–190 |
| t=270 s 一次台阶 | 6.10 → 6.49 MB | 21.9 → 24.8 MB | 190 → 224 |
| t=270–600 s | 6.49–6.62 MB | 24.8–25.0 MB | 223–225 |

台阶后再观察 5 分钟（`nsm-07-h.txt`，提权采样）：private 6.59–6.67 MB、handles 223–225，
**无持续增长**。台阶量级（+0.39 MB ≈ 3 × 128 KB）与 ETW 实时会话缓冲池从
`MinimumBuffers=4` 扩到约 7 个 128 KB 缓冲一致（`etw.cpp:113-115`，上限 32 个 = 4 MB）。

复现测试（`nsm-07-i.txt`，空闲 5 分钟，15 s 一点）：private 6.02–6.32 MB、handles 186–190，
**没有出现台阶**。所以台阶由当时的流量决定（缓冲扩容），不是定时泄漏；上限由 `MaximumBuffers=32` 封顶。

同一轮 `logman query -ets` 在中等完整性 shell 里返回 0 条，提权重查 `session_count=1`
（`nsm-07-h.txt`）：会话全程存活，`session=0` 是查询权限问题，不是会话掉了。

## 已知边界

- `R` 行偶见 `pid <n>` 形式的退化名（进程退出瞬间或路径查询失败）。
  C# 基准版同样有该兜底（`ProbeRunner.cs:251` 返回 `$"pid {pid}"`），属继承行为，本单不改。
- provider 不发 UDP 事件（`udp_ev=0`），QUIC/UDP 无法按进程归因；与 C# 版同源。
- 总量对账里 `stats_delta_rx` 含非 TCP 流量（QUIC 等），探针只统计 TCP 外网段；
  本次两者差 0.1% 属巧合式吻合，不能当作「UDP 已归因」的证据。
