# 06: 设置与托盘菜单

**What to build:** `%APPDATA%\NetSpeedMonitor\settings.json` 持久化刷新率、接口、单位、置顶、窗口位置。托盘右键菜单提供单位、接口、置顶、退出。双击标题区折叠成 280×28 单行，再双击还原。

**Blocked by:** 01

**Status:** ready-for-agent

- [ ] 修改刷新率/单位/置顶后重启程序，设置仍生效
- [ ] 双击折叠后只剩单行总速率，再双击还原
- [ ] 托盘右键「退出」是唯一退出路径，X 不退出
- [ ] settings.json 损坏时回落到默认值并正常启动
