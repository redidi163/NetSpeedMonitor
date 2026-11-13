# NetSpeedMonitor

C++20 + Win32 桌面应用（原生版为交付物），C#/WPF 版源码保留作行为基准。规格见 `SPEC.md`，工单见 `.scratch/net-speed-monitor/issues/`。

## 命令

- 原生构建：`native\build.bat`（vcvars64 + cl，产物 `native\bin\NetSpeedMonitor.exe`）
- 发布：构建后把 `native\bin\NetSpeedMonitor.exe` 复制到 `D:\githubitem\NetSpeedMonitor\`
- C# 基准版（仅历史参考）：`dotnet build` / `dotnet run --project src/NetSpeedMonitor` / `dotnet test`

## 结构

- `native/` C++ 交付物；工单 `native-0*.md`，与 C# 版同名工单区分
- `src/NetSpeedMonitor/` C#/WPF 历史基准，不再发布
- `tests/NetSpeedMonitor.Tests/` C# 版单元测试（历史）
- `.scratch/net-speed-monitor/issues/` 工单，一单一文件

## 约束

- 一次只做一个工单，验收判据不过不进入下一单。
- 源码入库，`publish/` 与 `bin/`、`obj/` 不入库。
- 改行为与改 `SPEC.md` 放同一个提交。
