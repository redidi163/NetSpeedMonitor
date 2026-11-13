# native-05: 交互（失焦收起、拖动、球跟随）

**What to build:** 面板点开后点别的窗口自动收起；面板可拖动，位移小于阈值不动；拖动结束后球移到面板左上角；关闭按钮与托盘切换保持可用。

**Blocked by:** native-04

**Status:** done

- [x] 展开/收起尺寸正确
      （`native-05-open.png`：panel `182,60 420x522`、ball `60,60 114x114`，
      144 dpi 下对应 280×348 / 76×76 DIP）
- [x] 失焦收起，250 ms 内
      （`nsm-05-latency.txt`：`foreground_changed_at_ms=599 panel_hidden_at_ms=616 delta_ms=17`；
      `nsm-05-test.txt`：切到 notepad 后 `panel_visible=False`）
- [x] 位移 <6 px 不算拖动
      （`nsm-05-test.txt`：`after 3px drag: 182,60 420x522`，窗口没动）
- [x] 拖动生效，拖完球落在面板左上角
      （`nsm-05-test.txt`：拖 120×90 后 `panel=302,150 420x522 ball=302,150 114x114`；
      `native-05-dragged.png` 可见球压在面板左上角）
- [x] 关闭按钮、托盘/球切换仍可用
      （`nsm-05-test.txt`：`after close click: panel_visible=False`；
      重新 `WM_APP+2` 后 `panel_visible=True`）
- [x] 无回归
      （exe 276,480 → 276,992 B；private 7.08 MB；`build.bat` `crlf=28 lf_only=0 non_ascii=0`）

## 实现

- 面板保持 `WS_EX_NOACTIVATE`：点它不抢焦点，代价是永远收不到 `WM_ACTIVATE`。
  收起改为「开面板时记下当前前台窗口，200 ms 定时器比对 `GetForegroundWindow()`，
  变了就 `Show(false)`」；拖动期间跳过检查，避免拖到别的窗口上误收。
- 拖动：`WM_LBUTTONDOWN` 记屏幕坐标与窗口原点并 `SetCapture`；`WM_MOUSEMOVE` 位移 ≥6 DIP
  （144 dpi 下 9 px）才置 `dragged_` 并 `SetWindowPos`；`WM_LBUTTONUP` 松开捕获，
  真拖动过才 `PostMessage(host, WM_APP+3, x, y)`。
- `main.cpp` 的 `WM_APP+3` 调 `ball.MoveTo(x, y)`；`BallWindow` 新增 `MoveTo`。
- 关闭按钮改为按下时记录命中区（`closePressed_`），抬起且没拖动过才关，避免「拖完顺带关窗」。

## 已知边界

- 面板开着时任何前台窗口变化都会收起，包括系统弹窗抢焦点；这是「失焦收起」的直接后果。
- 拖动后位置不持久化（`SPEC.md` 定案 16：无配置文件），重启回到默认位置。
