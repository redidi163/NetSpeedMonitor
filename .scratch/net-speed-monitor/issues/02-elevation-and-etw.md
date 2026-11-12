# 02: 提权 + ETW 进程速率

**What to build:** 程序清单声明 `requireAdministrator`，启动时请求 UAC。提权后建立 `Microsoft-Windows-Kernel-Network` 实时 ETW 会话，按 PID 累计事件 10/11（TCP 收/发）与 26/27（UDP 收/发），映射到 exe 名，在窗口下方显示 Top 5：exe 名 + 速率条 + MB/s。

**Blocked by:** 01

**Status:** ready-for-agent

- [ ] 启动时出现 UAC 提示，同意后进程处于提权状态
- [ ] 浏览器播放 1080p 视频时，浏览器 exe 稳定出现在 Top 3
- [ ] ETW 汇总的总字节速率与同窗口 `GetIfTable2` 总量误差 <15%
- [ ] 程序退出后 `logman query -ets` 查不到残留会话
- [ ] 首次实测确认：非管理员建立内核 provider 会话确实失败（用于 Ticket 04 的降级判据）
