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

## 界面验证（本机实测，native-10 踩出来的）

- **验证脚本必须提权运行**：交付物 manifest 是 `requireAdministrator`，中等完整性进程用
  `SendInput` / `mouse_event` 打不到它的窗口——调用返回 1，`GetAsyncKeyState` 却一直是
  `0x0000`，跟“程序不响应”表现完全一致。用 `Start-Process -Verb RunAs` 起脚本后立刻正常。
- **夹具先自证再验产品**：注入点击前先断言 `WindowFromPoint(落点) == 目标窗口`。落点被别的
  顶层窗口盖住时点击会打到别处，看起来同样像“点了没反应”。
- **移植机制先对齐前提**：C# 基准的 `Deactivated` 来自 WPF 窗口会被 `Activate()` 激活；原生版用
  `WS_EX_NOACTIVATE` 不抢焦点，这个前提不存在，“失去激活”不能直译成“前台窗口变了”。
- **验证要打初始状态**：除“操作后是否变化”，还要记录打开时的状态，否则“刚打开就被自己收起”
  这类竞态会从判据里漏过去。
