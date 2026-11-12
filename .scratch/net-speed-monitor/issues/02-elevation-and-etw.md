# 02: 提权 + ETW 进程速率

**What to build:** 清单用 `asInvoker`（不能用 `requireAdministrator`，否则工单 04 的受限模式没有入口）。启动后检测是否提权：已提权则建立 `Microsoft-Windows-Kernel-Network` 实时 ETW 会话，按 PID 累计事件 10/11（TCP 收/发）与 26/27（UDP 收/发），解析源/目的地址区分回环与外网，按归属规则 C 聚合，在窗口下方显示 Top 5：exe 名 + 速率条 + MB/s。未提权则显示一行提示，不启动 ETW。

**Blocked by:** 01

**Status:** in-progress

- [x] 非管理员建内核 provider 会话的失败已实测：`logman` 返回“拒绝访问”
- [x] ETW spike 验证事件可取 PID 级字节数（9 个 PID，含 msedge 5.86 MB）
- [ ] 已提权时 Top 5 出现，浏览器播放 1080p 时浏览器进程进 Top 3
- [ ] 外网段汇总与同窗口 `GetIfTable2` 误差 <15%
- [ ] 本地代理（verge-mihomo）不计入 Top 列表，回环流量归发起方
- [ ] 程序退出后 `logman query -ets` 查不到残留会话
- [ ] 未提权启动时不启动 ETW，界面给出明确提示
