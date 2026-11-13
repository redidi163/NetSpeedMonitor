# 07: 打包部署

**What to build:** `dotnet publish` framework-dependent win-x64 输出到 `D:\githubitem\NetSpeedMonitor`，附启动脚本；按 `SPEC.md` 的三条验收判据做一次完整手工验收。

**Blocked by:** 02, 03, 04, 05, 06

**Status:** done

- [x] 目标目录双击启动脚本即可运行，不依赖源码目录
      （`启动 NetSpeedMonitor.bat` 拉起后 `Get-Process.Path` = `D:\githubitem\NetSpeedMonitor\NetSpeedMonitor.exe`，
      发布目录 15 个文件 6.3 MB；`logman query -ets` 只有 1 个 `NetSpeedMonitor` 会话，无残留）
- [x] 验收判据 1：总量误差 <10%
      （`nsm-07-reconcile.txt`：40 秒 TUNA ISO 下载，界面累计 91.73 MB，`Get-NetAdapterStatistics`
      同窗口累计 94.47 MB，误差 2.9%。口径说明：空闲时任务管理器与程序都是 0.00，比值无定义，
      所以改成「负载下与独立 API 对账」，与 02 的载荷对载荷一致）
- [x] 验收判据 2：浏览器播 1080p 进 Top 3
      （`nsm-07-panel-edge.png`：Edge 播 `Big_Buck_Bunny_1080_10s_30MB.mp4`，进程列表第一行
      `msedge.exe 2.2M`，总下行 2.28 MB/s）
- [x] 验收判据 3：拒绝提权进受限模式且不崩
      （`nsm-07-restricted.png`：直接起 exe 不提权 → 黄条「未提权，进程速率不可用」+「以管理员重启」，
      进程连接 98 条按连接数排序，页脚「受限模式 · 进程按连接数排序」，进程存活未崩）
- [x] 发布后连续采样未见句柄/线程泄漏
      （两段共 13 分钟，同一构建：
      `nsm-07-rss.txt` 09:09:32–09:17:03：rss 183.1→194.2→183.2→186.4→182.5→184.2→185.8→185.9→184.1→184.8 MB；
      `nsm-07-rss2.txt` 09:20:32–09:25:33：rss 190.6→198.9→196.7→195.5→204.2 MB，private 107→119.3 MB。
      handles 795–812、threads 27–37 全程不涨，且第一段出现过 private 111.5→100.6 的 GC 回落，
      说明回收正常。托管堆在两次 GC 之间有 +1.2 MB/min 的上漂，13 分钟不足以定性，见「遗留」）
- [x] `git status` 干净，构建产物未入库
      （发布输出在 `D:\githubitem\NetSpeedMonitor`，不在仓库内；提交前 `git status --short` 为空）

## 实现

- 发布命令与 `AGENTS.md` 一致：
  `dotnet publish src/NetSpeedMonitor -c Release -r win-x64 --self-contained false -o D:\githubitem\NetSpeedMonitor`。
- 启动脚本 `启动 NetSpeedMonitor.bat`：CRLF + 纯 ASCII（实测 `crlf=3 lf_only=0 non_ascii=0`），
  `%~dp0` 指自身目录，内部 `Start-Process -Verb RunAs` 一次 UAC 直接进提权态，
  避免每次先落受限模式再点「以管理员重启」。
- 发布目录 6.3 MB（framework-dependent；`Microsoft.Diagnostics.Tracing.TraceEvent.dll` 3.25 MB 占大头）。
- 判据 1 的采样方式：界面 `DownMain` 文本每秒读一次（UIA `AutomationId=DownMain`），
  同一 tick 内读 `Get-NetAdapterStatistics -Name 'WLAN 2'` 的 `ReceivedBytes+SentBytes` 增量，40 个 tick 各自求和后比对。

## 验证脚本

`%TEMP%\nsm-07-reconcile.ps1`（总量对账，提权跑）、`%TEMP%\nsm-07-rss.txt` 与 `nsm-07-rss2.txt`
（两段 RSS 采样）、`%TEMP%\nsm-06-shots.ps1`（球态/面板截图）。截图与日志都在 `%TEMP%`。

## 遗留

- 判据 2 的 1080p 源是 `test-videos.co.uk` 的 30 MB 样片，不是真实视频网站；真实站点（B 站/YouTube）
  需要登录或手势才能起播，自动化验不了，只能手测。
- 09:17:33 那次实例退出原因未查明：Application 日志在 09:16–09:19 无 `Application Error` /
  `.NET Runtime` / WER 记录，Security 日志无 4689（审核策略未开）；34 秒后出现新实例 PID 57316。
  已单独验证「再启动一个实例」不会杀掉在跑的实例（新 PID 17668 在 4 秒后自行退出，原 PID 57316 存活），
  所以不是单实例互斥逻辑的问题。最可能是人工走托盘「退出」后重启，但无法证实。
- 单实例互斥等待 4 秒：第二个实例会白等 4 秒才退出，双击两次会有 4 秒延迟感。
- 托管内存长跑未定性：13 分钟两段采样里 private 从 100.6 MB 上漂到 119.3 MB（约 +1.2 MB/min），
  期间有 GC 回落，句柄/线程稳定。要判定是否真泄漏，需要 1 小时以上的连续采样；下次开机常驻时补测。
