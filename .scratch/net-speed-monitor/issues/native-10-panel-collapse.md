# native-10: 面板失焦收起失效

**What to build:** 面板打开后点击面板外的窗口必须收起。当前实现只比较「前台窗口是否变化」，点回「打开面板前就是前台的那个窗口」时前台不变，面板永远不收起。

**Blocked by:** native-09

**Status:** done

## 根因

面板用 `WS_EX_NOACTIVATE` 创建（不抢焦点），所以它从不成为前台窗口。收起判定是 200 ms 轮询
`GetForegroundWindow() != foregroundAtOpen_`，而 `foregroundAtOpen_` 就是打开面板时的前台窗口。

用户的操作序列正好命中盲区：

1. Edge 是前台窗口；
2. 点悬浮球打开面板，球是 `WS_EX_NOACTIVATE`，前台仍是 Edge；
3. 点回 Edge，Edge 本来就是前台，`GetForegroundWindow()` 不变；
4. 轮询看到 `foreground == foregroundAtOpen_`，判定“没失焦”，不收起。

切到别的窗口（如 cmd）能收起，是因为那次前台真的变了。所以故障不是“全都不生效”，而是
“只有点回原窗口不生效”。

## 改动

- `panel.h`：新增 `MouseHookProc` / `InstallMouseHook` / `RemoveMouseHook` / `IsOurWindowAt`、
  `hookOwner_`、`mouseHook_`、`foregroundAtOpen_`、`openedAt_`。
- `panel.cpp`：
  - `Show(true)` 记下 `foregroundAtOpen_` 与 `openedAt_`，启动 200 ms 轮询，安装 `WH_MOUSE_LL`；
  - `Show(false)` 先卸钩子再隐藏，避免关闭后残留的 `PostMessage` 影响下一次打开；
  - 钩子回调只在左/右/中键按下时做一次命中测试：面板按矩形判定（分层窗口圆角处
    `WindowFromPoint` 会穿透），其余窗口按 `GetWindowThreadProcessId` 判定是否本进程；
    落在面板外才 `PostMessage(WM_APP+4)`；
  - `WM_APP+4` 与 `WM_TIMER` 两条收起路径都要求 `visible_ && !dragging_`；
  - 轮询加 500 ms 开启宽限期：`--panel` 启动时前台还在切换，无宽限期会把面板刚打开就收起。
- `SPEC.md`：定案 12 补充“含点回打开面板前就是前台的那个窗口”，架构节写明三条收起路径与
  为什么不能用 `WM_ACTIVATE`，新增验收判据 6。

## 判定依据

| # | 判据 | 实测 | 结论 |
|---|---|---|---|
| 1 | 点面板外、前台不变时 ≤250 ms 收起 | `hidden_after_ms=1`，`foreground_unchanged=True` | 通过 |
| 2 | 点面板内不收起 | `visible=True` | 通过 |
| 3 | 点悬浮球切换收起 | `hidden_after_ms=0` | 通过 |
| 4 | 前台变化（alt-tab 类）收起 | notepad 抢前台后 `visible=False` | 通过 |
| 5 | 无操作不误收起 | 1.5 s 后 `visible=True` | 通过 |
| 6 | 12 次开合不泄漏句柄 | 每轮 `handles=225`，12 轮不变 | 通过 |
| 7 | 打开后不被宽限期外的误判收起 | `--panel` 首次 `open: visible=True` | 通过 |

判据 1 能排除轮询贡献：轮询间隔 200 ms、宽限期 500 ms，而实测 1 ms 收起，只能是钩子。

## 验证方法上的坑（复现时必读）

本机从中等完整性进程注入的鼠标事件**打不到提权窗口**：`SendInput` 返回 1、`SetCursorPos` 生效、
`GetAsyncKeyState` 却一直是 `0x0000`，面板 ✕ 与悬浮球都没反应。最早几轮“点击不收起”的结论全部
作废。

用 `Start-Process -Verb RunAs` 以管理员身份跑验证脚本后，`SendInput` 正常投递，✕ 与悬浮球立刻响应。
原生版 manifest 是 `requireAdministrator`，所以**界面验证脚本必须以提权身份运行**。

另：`WH_MOUSE_LL` 的安装句柄无法从外部进程读取，验证用临时日志确认过一次
（`install hook=000000007FC3174F err=0`），日志代码已随验证结束删除。

## 证据

日志：`%TEMP%\opencode\nsm-10-k.txt`（前台不变时收起）、`nsm-10-l.txt`（12 次开合句柄）、
`nsm-10-j.txt`（点击内外、球切换、前台变化）、`nsm-10-i.txt`（消息级收起与安装句柄）。

```
setup: panel=200,60 420x522 ball=60,60 114x114
console_foreground: ok=True foreground=20711680 is_console=True
ball_click: panel_shown_after_ms=4 foreground=20711680 is_console=True
click_on_already_foreground: hidden_after_ms=1
foreground_unchanged=True before=20711680 after=20711680
verdict: hook_only=True
```

```
cycle1: shown=True closed=True handles=225
...
cycle12: shown=True closed=True handles=225
final: handles=225 priv_MB=6.52 threads=6
```

被测二进制：`native\bin\NetSpeedMonitor.exe`，284,672 B。

## 已知边界

- 钩子只在面板可见期间存在；安装失败（返回 NULL）时退化为纯轮询，即 native-09 的行为。
- 点击面板圆角外的透明像素算“面板外”，会收起面板——按可见形状判定符合预期。
- 全屏应用里点一下会先收起面板（读数为全屏让路），这是全屏规则的副作用，不是收起逻辑。
- 轮询仍会处理 `--panel` 与托盘菜单的焦点回填，宽限期 500 ms 只压住打开瞬间。
