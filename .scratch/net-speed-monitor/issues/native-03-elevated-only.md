# native-03: 只允许提权运行

**What to build:** 取消受限模式。非提权时程序不运行；提权由 exe manifest 的 `requireAdministrator` 强制，双击即弹 UAC，拒绝则进程不创建。删除所有「未提权」分支与文案。

**Blocked by:** native-02

**Status:** done

- [x] exe 内嵌 manifest，`requestedExecutionLevel level="requireAdministrator" uiAccess="false"`
      （`mt -inputresource:bin\NetSpeedMonitor.exe;#1` 导出：`level="requireAdministrator"`、`uiAccess="false"`；
      exe 280,064 B，比无 manifest 时多 512 B）
- [x] 非提权启动被加载器拦下，不产生窗口
      （中等完整性 shell 里 `[Diagnostics.Process]::Start(exe)` 抛
      「请求的操作需要提升」，`Get-Process NetSpeedMonitor` 计数 0；
      `runas /trustlevel:0x20000` 退出码 1、无新进程）
- [x] 提权启动正常，ETW 会话照常建立
      （`Start-Process` 走 ShellExecute 自动提权，PID 45196；中等 shell 读不到 `.Path`；
      提权脚本读到 `path=...\native\bin\NetSpeedMonitor.exe`、`priv=6.03 MB`；
      `logman query -ets` 列出 `NetSpeedMonitorNative`）
- [x] 代码里不再有受限分支
      （`grep -n 'elevated' native\src\main.cpp` 只剩 `IsElevated()` 定义与启动守卫；
      `app.elevated` 字段、托盘「（受限）」后缀、页脚「未提权，进程速率不可用」全部删除）

## 实现

- `build.bat` 链接参数：`/MANIFEST:EMBED /MANIFESTUAC:"level='requireAdministrator' uiAccess='false'"`。
  只写 `/MANIFESTUAC` 不生效——本机 VS 18 的 link.exe 默认 `/MANIFEST:NO`，
  不加 `/MANIFEST:EMBED` 时 exe 没有 `.rsrc` 段，`mt` 报「不包含资源区域」。
- `wWinMain` 在诊断开关之后、建窗口之前保留守卫 `if (!IsElevated()) return 2;`。
  manifest 生效时该路径不可达，仅防御被去掉提权的启动方式。
- `app.etw.Start()` 改为无条件调用：提权是前置条件，启动失败仍由页脚显示真实错误。

## 代价

- 非提权 shell 里 `Start-Process native\bin\NetSpeedMonitor.exe` 返回 740；
  `--probe` 等诊断开关同样要求提权（`--etw-probe` 本来就需要）。
- 构建前必须结束正在运行的实例，否则 `LNK1104`；本机中等 shell 可以 `Stop-Process` 掉提权实例。

## 文档

- `SPEC.md` 定案 3、定案 13、数据源表、判据 3 同步改写。
- `.scratch/net-speed-monitor/issues/04-restricted-mode.md` 标 superseded，C# 代码保留作历史。
