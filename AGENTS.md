# NetSpeedMonitor

WPF / .NET 10，单进程桌面应用。规格见 `SPEC.md`，工单见 `.scratch/net-speed-monitor/issues/`。

## 命令

- 构建：`dotnet build`
- 运行：`dotnet run --project src/NetSpeedMonitor`
- 测试：`dotnet test`
- 发布：`dotnet publish src/NetSpeedMonitor -c Release -r win-x64 --self-contained false -o D:\githubitem\NetSpeedMonitor`
- 原生构建：`native\build.bat`（vcvars64 + cl，产物 `native\bin\NetSpeedMonitor.exe`）

## 结构

- `src/NetSpeedMonitor/` 应用本体（C# 版，作为行为基准保留）
- `tests/NetSpeedMonitor.Tests/` 单元测试
- `native/` C++ 重写版；工单 `native-0*.md`，与 C# 版同名工单区分
- `.scratch/net-speed-monitor/issues/` 工单，一单一文件

## 约束

- 一次只做一个工单，验收判据不过不进入下一单。
- 源码入库，`publish/` 与 `bin/`、`obj/` 不入库。
- 改行为与改 `SPEC.md` 放同一个提交。
