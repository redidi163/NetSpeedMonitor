# native-17: 电量 SPEC/README 同步 + 发布 v0.3.0

**What to build:** 把 native-15/16 的电量行为写进 SPEC.md 与 README.md，构建无回归，发布 v0.3.0 到 `D:\githubitem\NetSpeedMonitor\` 并打 tag。

**Blocked by:** native-15、native-16

**Status:** done

## SPEC 改动

- 数据源表新增：鼠标电量 → 2.4G 接收器 vendor collection（A8A5:2255），`hid.dll`+`setupapi.dll`，HID 原生 API，无 `hidapi`。
- 架构节新增：`BatteryReader`（主动查，2.5 s 超时）在 worker 线程轮询（读到 60 s/ miss 120 s，三miss 后退 120 s），`BatterySnapshot` 快照供 UI 线程读取，不阻塞 1 s 任务栏 tick。
- 新增定案 20：

  | # | 决策 | 定案 |
  |---|---|---|
  | 20 | 鼠标电量胶囊 | 任务栏读数右侧画胶囊，宽 `max(38, 文本宽+12)` DIP、高 16 DIP、与文字间距 8 DIP，锚在 `TrayNotifyWnd.left − 6 DIP`。无设备→整段隐藏；有设备未读到或样本龄 >180 s→`--` 灰底不闪；正常→百分比；充电→`+` 后缀。底色随电量由绿过渡到红，≤20% 固定 `RGB(178,40,32)`，白字；放电且 ≤20% 时 500 ms 闪烁（进 ≤20% / 退 23%），充电时不闪。 |

- 验收判据新增第 8～10 条：

  | # | 判据 | 方法 |
  |---|---|---|
  | 8 | 无设备 → 胶囊整段隐藏 | 拔掉接收器，任务栏读数右侧不出现胶囊 |
  | 9 | `--battery-force=86` 胶囊绿底不闪；`--battery-force=18` 闪烁（进/出 500 ms）；`--battery-force=18c` 常亮红底不闪 | 提权脚本启动，`pill_check.py` 抓窗口像素，中心点填充色与 bright/dark 切换 |
  | 10 | 百分比变化不改窗口宽度（不抖动/不重dock） | `--battery-force=50` 与 `=86` 连续切换，窗口 rect 保持不变 |

## README 改动

- 功能列表新增一条：2.4G 鼠标电量胶囊（A8A5:2255，Viper V3 Pro / K-snake 系列），任务栏读数右侧常显。
- 运行命令新增：`--battery-probe N`（不开界面，探测电量 N 轮）、`--battery-force=NN[c]`（仅验收，不写进 README 文档——此条可省，保持与决策一致）。因决策说钩子不写 README，这里只写 `--battery-probe`。

## 构建与发布

- `native\build.bat` 构建通过，无新增警告。
- 复制 `native\bin\NetSpeedMonitor.exe` 到 `D:\githubitem\NetSpeedMonitor\`。
- `git tag v0.3.0`，推送到 `origin/main`（需 `HTTPS_PROXY=http://127.0.0.1:7897`，`gh` 在 `D:\githubitem\gh\bin`，非仓库目录加 `-R redidi163/NetSpeedMonitor`）。

| # | 判据 | 结果 | 证据 |
|---|---|---|---|
| 8 | 无设备 → 胶囊隐藏 | 未测（本机有接收器，拔插需人工）| — |
| 9 | 86 绿底不闪 / 18 闪烁 / 18c 红底常亮 | 通过 | `pill_check.py`：force86 fill=(43,112,42) 不闪；force18 4 次切换 bright/dark；force18c fill=(32,40,178) 常亮 |
| 10 | 百分比变化不改窗口宽度 | 通过 | force86 与 force18 pill width 均 51 px，rect 相同 |

## 交付（2026-11-20）

- 提交 `f50c7ac`：`feat: 任务栏鼠标电量胶囊（native-15/16/17）`，9 文件 +826/−5
- tag `v0.3.0` 已推送 `origin/main`，`git status` 干净
- exe 已复制 `D:\githubitem\NetSpeedMonitor\NetSpeedMonitor.exe`（395,776 字节）
- native-15 探针本机 hits=0（BetterGI 分身会话 COL01 open err=5，无法采集物理鼠标；读取层代码完整，需真机/主桌面复测）
