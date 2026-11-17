# NetSpeedMonitor 规格

## 目标

常驻托盘的小窗，实时显示默认路由网卡的上下行速率，以及当前占用带宽最大的进程（Top 5）；
任务栏右侧常显一行网速 / 内存 / CPU 读数。

## 非目标（第一期）

- 历史数据落盘、按天统计
- 连接级明细（远端 IP:端口）
- 开机自启
- 进程图标、友好名映射
- 多网卡合计

## 定案

| # | 决策 | 定案 |
|---|---|---|
| 1 | 形态 | 托盘常驻 + 悬浮球（76×76，上↑下↓）；单击展开面板，失焦收起 |
| 2 | 位置 | 源码 `默认项目\NetSpeedMonitor`，产物装 `D:\githubitem\NetSpeedMonitor` |
| 3 | 权限 | 必须提权运行；拒绝 UAC 则不启动。原生版用 manifest `requireAdministrator` 强制，不做受限模式 |
| 4 | 统计口径 | 只算默认路由接口，每 5 s 重查；不做接口下拉，选择不持久化 |
| 5 | 粒度 | 进程级，同 exe 合并 |
| 6 | 刷新 | 固定 1s；60 秒迷你走势，不落历史 |
| 7 | 技术栈 | .NET 10 WPF + Microsoft.Diagnostics.Tracing.TraceEvent |
| 8 | 单位 | 只用 KB/s（1 位小数），不显示 Mbps 副行 |
| 9 | 排序 | 3 秒滑动平均排序，显示 1 秒瞬时值 |
| 10 | 交付 | framework-dependent；一期不做自启 |
| 11 | 验收 | 总量误差 <10%、浏览器进 Top 3、拒绝提权不启动、载荷对账 <5% |
| 12 | 窗口 | 球 76×76，面板 280×348（总量 + 走势 + Top 5）；面板可拖动（位移 ≥6 px 才生效），拖完球落在面板左上角；点面板外任一窗口 250 ms 内收起（含点回打开面板前就是前台的那个窗口）；托盘单击切换显示 |
| 13 | 受限模式 | 不做。非提权直接不启动（C# 版 04 工单已 superseded） |
| 14 | 边界 | 5s 重查默认路由；唤醒重建会话；断网显示 0；进程退出延迟 5s 移除 |
| 15 | 进程名 | 只显示 exe 名，无图标、无友好名 |
| 16 | 配置 | 无。不落盘任何状态；X = 隐藏，退出走托盘 |
| 17 | 任务栏读数 | 任务栏右侧常显一行 `↑41.0 ↓1496.0 KB/s · 内存 42% · CPU 12%`；字体 11 pt，颜色跟系统浅/深色（启动读一次）；无配置项、无右键菜单、无拖拽 |
| 18 | 全屏 | 前台窗口覆盖所在显示器时，任务栏读数与悬浮球都隐藏，退出全屏后 1 s 内恢复；任务栏自动隐藏时读数一并隐藏 |

## 数据源

| 用途 | 来源 | 权限 |
|---|---|---|
| 接口总量 | `GetIfEntry`（iphlpapi） | 普通 |
| 进程字节 | ETW `Microsoft-Windows-Kernel-Network`，事件 10/11（TCP 收/发）、26/27（UDP 收/发） | 管理员 |
| 进程名 | `Process.ProcessName` | 普通 |
| CPU 占用 | `GetSystemTimes` 两次差值（kernel+user−idle） | 普通 |
| 内存占用 | `GlobalMemoryStatusEx().dwMemoryLoad` | 普通 |

ETW 事件 payload 含 PID、字节数与源/目的地址，是本机唯一可用的 PID 级字节来源。
连接数来源（`GetExtendedTcpTable`）随受限模式一并取消，不再采集。

## 归属规则（本地代理环境）

本机走 `verge-mihomo` 系统代理，回环会把同一份字节记两次（实测 10 秒：msedge 收 5.86 MB，代理收 6.41 MB，两者是同一批数据）。规则：

- 解析事件源/目的地址，区分回环段与外网段。
- 回环段归发起方进程，外网段归自己。
- 同一窗口内同时有回环和外网流量的进程判定为本地代理，其流量不计入 Top 列表，避免与下游重复。
- 与网卡总量对账时只累加外网段。
- 误判兜底：后续在设置里提供“忽略进程”名单。


## 架构

```
EtwSampler (后台线程) ──> ProcessAggregator (3s 滑窗) ──> UI (1s 刷新)
AdapterSampler (1s)   ──> 总量 + 60s 走势
AdapterSampler (1s)   ──> TaskbarWindow：任务栏一行读数（网速 + 内存 + CPU）
```

- 采样与 UI 解耦，UI 只读快照。
- 聚合键：exe 路径；PID 复用用进程启动时间区分。
- 排序用 3 秒滑窗均值，数字显示 1 秒瞬时值。
- 均值低于 1 KB/s 的行不显示（keepalive 噪声）；进程停止后随滑窗衰减，3 秒内移出列表。
- 底部速率是载荷口径（TCP/UDP payload），比上方网卡速率低约 6%，文案标「载荷」。
- 会话生命周期：启动建立，唤醒后销毁重建，退出时停止。
- 唤醒：窗口收到 `WM_POWERBROADCAST` 的 `PBT_APMRESUMEAUTOMATIC` / `PBT_APMRESUMESUSPEND` 后重建
  ETW 会话，并重置滑窗与网卡基线（3 秒内重复的 Resume 只处理一次）。
- 断网（无默认路由）时总量归零、清空 Top，页脚显示「断网 · 等待默认路由恢复」；
  恢复后重置滑窗，避免把断网区间的差值算进第一帧。
- 不做接口选择：统计口径恒为默认路由接口（见定案 4），没有下拉、没有配置文件。
- 面板收起有三条触发路径，任一命中即收起：
  1. `WH_MOUSE_LL` 低级鼠标钩子，只在面板可见期间安装，隐藏即卸载。左/右/中键按下时用
     `WindowFromPoint` 判断落点是否属于本进程，点面板外才 `PostMessage(WM_APP+4)`，
     收起逻辑跑在 UI 线程而不是钩子回调里。
  2. 前台轮询 200 ms，处理 alt-tab、Win+D 这类不产生点击的焦点变化；面板打开后 500 ms
     内不判定，避免托盘菜单关闭或 `--panel` 启动时焦点回填被误判成失焦。
  3. 点面板 ✕，或点悬浮球（`WM_APP+2` 切换）。
  面板保持 `WS_EX_NOACTIVATE`（不抢焦点），所以不能靠 `WM_ACTIVATE` 判定失焦：面板从不
  成为前台窗口，而点回“打开面板前就是前台的那个窗口”时前台不会发生变化，只比前台快照
  永远看不到这次点击。native-10 在本机复现了这条路径，钩子方案是对它的修法。
- 任务栏读数用顶层窗口贴在 `TrayNotifyWnd.left − 6 DIP`，样式
  `WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT`（点击穿透、不抢焦点）。
  TrafficMonitor 用 `SetParent` 把窗口挂进 `Shell_TrayWnd`（`TaskBarDlg.cpp`），但本机 Win11 26200
  上 `SetParent` 返回成功却**不被合成**：子窗口画纯红底，屏幕上仍是任务栏自己的像素；同一窗口改为
  顶层悬浮后立刻可见。故不走子窗口路径，`SPEC` 记录这条实测结论，避免后来者按原版重做一遍。
- 隐藏条件（任务栏读数与悬浮球共用 `FullscreenOnMonitor()`）：前台窗口矩形覆盖参照窗口所在
  显示器（±2 px 容差，排除 `Progman`/`WorkerW`/任务栏自身与本进程窗口）。任务栏读数另需任务栏
  不在自动隐藏滑出状态（此时是滑出屏幕而不是 `SW_HIDE`）。悬浮球只记用户意图，全屏仅临时压住
  显示，托盘菜单文案仍按用户选择显示。

## 验收判据

1. 负载下总量与独立 API 对账误差 <10%（`Get-NetAdapterStatistics` 同窗口累计 vs 界面累计）。
   空闲时任务管理器「性能」页与程序都是 0.00，比值无定义，不能作为判据。
2. 浏览器播 1080p 视频，浏览器进程稳定进 Top 3。
3. 拒绝提权时不启动：进程不创建窗口，无残留实例（原生版 manifest 直接拦在加载器）。
4. 进程字节对账用「载荷对载荷」：固定 12 MB 下载，ETW 记在该 PID 上的外网字节与 curl
   自报的 `size_download` 误差 <5%。不能用 `GetIfTable2` 做参照物——它统计 L2 线缆字节
   （含 802.11 帧头、加密开销、MAC 层重传），与 ETW 的载荷字节不是同一口径。
5. 任务栏读数：右边缘与 `TrayNotifyWnd.left` 间隙 ≥0、≤12 px；垂直居中偏差 ≤2 px；
   CPU 与 `\Processor(_Total)\% Processor Time` 同窗口对照误差 ≤3 个百分点，内存 ±1 个百分点；
   与悬浮球同 tick 数值一致；前台全屏窗口覆盖显示器时读数与悬浮球都不可见，退出全屏后 2 s 内恢复；
   两者都不是前台窗口。
6. 面板收起：点面板外任一窗口 ≤250 ms 收起，且**前台窗口不变时同样收起**（点回打开面板前的
   前台窗口）；点面板内不收起；点悬浮球切换收起；12 次开合后进程句柄数不增长。

## 部署

```
native\build.bat
copy /y native\bin\NetSpeedMonitor.exe D:\githubitem\NetSpeedMonitor\
```

- 单个 exe（约 284 KB），`/MT` 静态链接，不依赖 .NET 或 VC 运行时。
- exe 内嵌 manifest `requireAdministrator`：双击 exe 或 bat 都会弹 UAC，拒绝则不启动。
- 同目录放 `启动 NetSpeedMonitor.bat`（CRLF、纯 ASCII、`%~dp0`），内容只有一行 `start`。
- 发布目录不是 git 仓库，源码只留在 `默认项目\NetSpeedMonitor`。

## 风险

- ETW 内核 provider 需提权，非管理员无法建立实时会话（待 Ticket 02 实测确认）。
- 睡眠唤醒后内核会话不保证存活，必须重建。
- TraceEvent 依赖 nuget.org，离线环境无法还原。
