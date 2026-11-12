# 07: 打包部署

**What to build:** `dotnet publish` framework-dependent win-x64 输出到 `D:\githubitem\NetSpeedMonitor`，附启动脚本；按 `SPEC.md` 的三条验收判据做一次完整手工验收。

**Blocked by:** 02, 03, 04, 05, 06

**Status:** ready-for-agent

- [ ] 目标目录双击启动脚本即可运行，不依赖源码目录
- [ ] 验收判据 1：空闲总量与任务管理器误差 <10%
- [ ] 验收判据 2：浏览器播 1080p 进 Top 3
- [ ] 验收判据 3：拒绝提权进受限模式且不崩
- [ ] `git status` 干净，构建产物未入库
