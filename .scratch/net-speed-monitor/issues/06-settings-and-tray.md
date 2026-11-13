# 06: 悬浮球与设置收窄

**What to build:** 启动即为 76×76 悬浮球（上半 ↑、下半 ↓，单位只写 MB/s）；单击球展开 280×348 面板；
面板失焦（点其他地方、切窗口）收起回球；球可拖动，拖动不误触发展开；接口选择持久化。
明确不做：刷新率切换、单位切换、置顶开关、托盘右键菜单项、窗口位置持久化。

**Blocked by:** 05

**Status:** done

- [x] 单击球展开面板，尺寸从球态切到面板态
      （`nsm-06-final.txt`：ball_rect=60,60,114x114 → panel_rect=60,60,420x522；
      114/420/522 是 150% DPI 下的物理像素，对应 76/280/348 DIP）
- [x] 面板失焦收起，回到球态
      （`nsm-06-focus.txt`：22:49:19.006 `deactivated expanded=True` → 22:49:19.038 收起；
      同文件每 250 ms 采一次前台窗口，t=500 ms 前台已切到 PID 24344「任务管理器」、
      t=3500 ms 切到 PID 31904「协作客户端远程监控程序」，收起与前台切换同帧，不是自激）
- [x] 拖动球不误展开
      （`nsm-06-msg2`/`nsm-06-focus.txt` 日志：按下后位移 6 px 即走 `DragMove`，
      抬起时 `dragged=True` 不调用 `Expand`；纯点击 `dragged=False` 才展开）
- [x] 接口下拉打开时面板不收起
      （`nsm-06-dd.txt`：下拉 `state=Expanded` 时面板连续采样仍为 420x522 ≥1 s，
      期间无 `deactivated`；后续收起时日志为 `dropdown=False`，即收起由外部切窗口触发）
- [x] 接口选择重启后仍生效（05 已验收，`settings.json` = `{"AdapterIndex":36}`）

## 实现

- 单窗口两态：同一个 `MainWindow` 只切 `Width/Height` 与 `BallRoot`/`PanelRoot` 的
  `Visibility`，不额外开窗，托盘图标、定时器、ETW 会话都不重建。
- `Expand` 以球左上角为锚点，按 `SystemParameters.WorkArea` 钳制；`Collapse` 让球落在
  面板左上角（面板被拖动过也不会跳回旧位置）。
- 球同时要支持点击和拖动：`MouseLeftButtonDown` 记屏幕坐标，`MouseMove` 位移超过 4 DIP
  才 `DragMove`，`MouseLeftButtonUp` 时 `dragged=False` 才 `Expand`。
- 收起走 `Deactivated`；`AdapterPicker.IsDropDownOpen` 时跳过；X 隐藏窗口也会失焦，
  用 `_suppressCollapseUntil = Now + 1` 抑制一次，否则托盘唤出永远是球态。
- 删除 Mbps 副行，`DownMbps`/`UpMbps` 两个 TextBlock 一并移除，单位统一 MB/s。

## 验证脚本

`%TEMP%\nsm-06-focus.ps1`（展开 + 前台窗口采样）、`nsm-06-dd.ps1`（下拉守卫）、
`nsm-06-shots.ps1`（球态/面板截图）。全部提权运行，日志同目录 `.txt`。

真实鼠标注入需要“窗口跟随光标”才能命中：用户同时在操作鼠标，`SetCursorPos` 后位置立刻被改
（实测 117,117 → 634,137）；改用 `SetWindowPos` 把球挪到光标下再 `mouse_event` 点，命中率才稳定。
`PostMessage(WM_LBUTTONDOWN/UP)`、`PostMessage(WM_ACTIVATE)` 实测 WPF 不处理（日志无任何反应），
已放弃这条路。

## 未验证

- 球/面板在多显示器或 100%/200% DPI 下的钳制与落点，只做了 150% 单屏实测。
- 球拖到屏幕边缘时的 `ClampBall` 边界，只做了代码审查（拖动后位置由 `Left/Top` 回填）。
