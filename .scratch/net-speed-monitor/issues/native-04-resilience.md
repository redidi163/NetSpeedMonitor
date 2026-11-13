# native-04: 韧性（无下拉）

**What to build:** 统计口径固定跟随默认路由，删掉接口选择与 `settings.json`；唤醒后销毁并重建 ETW 会话；断网时总量归零、清空 Top、页脚提示，恢复后不把断网区间算进第一帧。

**Blocked by:** native-03

**Status:** done

- [x] 断网 5 s 内归零并显示断网文案
      （`native-04-offline.png`：标题「无接口」、0.00 / 0.00、进程列表空、
      页脚「断网 · 等待默认路由恢复」；
      `nsm-04-timing2.txt`：`disable_return_ms=3249 route_up=False`、`ui_offline_after_ms=17`）
- [x] 恢复后 5 s 内切回，且第一帧不出现异常大值
      （`nsm-04-timing2.txt`：`enable_return_ms=5811`、`route_back_after_ms=4197`、
      `ui_online_after_ms=35`——默认路由一出现，界面同一轮就切回；
      `native-04-recovered.png`：0.03 / 0.02 MB/s，Top 列表恢复，无尖峰）
- [x] 唤醒后重建 ETW 会话，重复 resume 只重建一次
      （向 `NSM.Host` 连发两次 `WM_POWERBROADCAST / PBT_APMRESUMEAUTOMATIC`，间隔 200 ms：
      `sessions before : 1` → `sessions after-resume : 1`，无残留、无重复；
      `native-04-resume.png` 页脚仍是 `ETW · EnableTraceEx2 · 4714 事件`）
- [x] 删除接口选择，无回归
      （`settings.cpp/h` 删除并从 `build.bat` 移除；exe 280,064 → 276,480 B；
      运行中 private 6.73 MB；面板仍 420×522）

## 实现

- `OnTick` 每 5 tick 调一次 `sampler.FollowDefaultRoute()`；`sampler.Index()==0` 即断网，
  进入/离开断网各 `aggregator.Reset()` 一次，断网期间跳过 `Sample()` 与 `Update()`。
- 页脚优先级：断网 > ETW 未启动 > 正常。
- `WM_POWERBROADCAST`：`etw.Stop()` + `etw.Start()`，同时 `ResetBaseline()` + `aggregator.Reset()`；
  用 `lastResumeTicks` 做 3 s 去重。
- 不再读写 `%LOCALAPPDATA%\NetSpeedMonitor\settings.json`；文件本身不动（C# 版仍在用）。

## 判据口径说明

断网/恢复的计时从 `Disable-NetAdapter` / `Enable-NetAdapter` **返回**开始，到标题栏像素变化为止。
启用网卡本身耗时 5.8 s，其中 4.2 s 后默认路由才重新出现；界面在路由出现后 35 ms 内切回。
最坏情况是路由出现后等下一个 5 s 重查点，即 ≤5 s。

## 验证脚本

`%TEMP%\nsm-04-test2.ps1`（负载 + resume + 断网恢复 + 截图）、`nsm-04-timing2.ps1`（毫秒级计时）、
`nsm-04-guard.ps1`（120 s 兜底重新启用 WLAN 2）。均需提权。
