# native-06: 打包部署

**What to build:** 发布目录 `D:\githubitem\NetSpeedMonitor` 只放原生 exe 和启动 bat；C# 版产物清空；双击即可启动。

**Blocked by:** native-05

**Status:** done

- [x] 发布目录只剩原生产物
      （`Get-ChildItem D:\githubitem\NetSpeedMonitor`：`NetSpeedMonitor.exe` 276,992 B、
      `启动 NetSpeedMonitor.bat` 120 B）
- [x] 发布 exe 与构建产物一致
      （两边 SHA256 都是 `B73C33D1CB089A9CDF4B940155A952F75D34BBC4F5633BCF5F7ACAADB5187136`）
- [x] 双击 bat 能启动，进程从发布目录运行
      （`cmd /c "D:\githubitem\NetSpeedMonitor\启动 NetSpeedMonitor.bat"` → PID 52236；
      提权脚本读到 `path=D:\githubitem\NetSpeedMonitor\NetSpeedMonitor.exe`、`elevated=True`、
      `priv=7.05 MB`）
- [x] ETW 会话正常
      （`logman query -ets` 有 `NetSpeedMonitorNative`）
- [x] 启动 bat 符合 Windows 约束
      （`crlf=3 lf_only=0 non_ascii=0`，内容只有 `start "" "%~dp0NetSpeedMonitor.exe"`）

## 实现

- C# 产物（14 个文件 6.10 MB，含 `amd64/` 与 TraceEvent 依赖）整体移到
  `%TEMP%\opencode\nsm-csharp-legacy-20261113\`，发布目录清空后只放原生 exe。
- `启动 NetSpeedMonitor.bat` 用 `WriteAllText` + `ASCII` 写，换行显式 `\r\n`。
  原生 exe 自带 `requireAdministrator` manifest，bat 里不再需要 `-Verb RunAs`。
- `SPEC.md` 部署段、`AGENTS.md` 命令与结构段同步改写。

## 备注

- 发布目录不是 git 仓库；回滚方式是从 `%TEMP%` 备份目录把 C# 产物拷回。
- 两个版本的单实例互不冲突（`Local\NetSpeedMonitor.Native` vs `Local\NetSpeedMonitor`），
  但 C# 版已不再发布，避免两个托盘图标同时出现。
