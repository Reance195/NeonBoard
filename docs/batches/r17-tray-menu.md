# R17 批次：托盘右键菜单霓虹化立卡清单

授权来源：维护者 2026-11-05 会话（明确授权代登记）。起因是维护者贴了一张托盘右键菜单
截图并说「这个右键菜单不好看」；本批按维护者当场选定的方案立卡：宿主自绘 Win32 霓虹弹窗，
主程序托盘 QMenu 一起统一皮肤。

```json
{
  "R17-01": {
    "stage": "R17",
    "title": "托盘右键菜单霓虹化：宿主自绘弹窗 + 主程序 QMenu 统一皮肤",
    "note": "起因（2026-11-05 维护者反馈）：NeonBoardHost.exe 的托盘右键菜单是纯 Win32 系统菜单（CreatePopupMenu + TrackPopupMenu），Windows 白底方框，和 NeonBoard 的赛博霓虹风完全脱节；主程序在非 hosted 模式下的托盘 QMenu 在 Windows 上同样走系统菜单，两处外观都不受主题系统管辖。目标：宿主改成自绘分层窗口霓虹弹窗（圆角 + 辉光 + 半透明底 + 悬停高亮），主程序 QMenu 套同一套皮肤令牌（QSS + 非原生绘制 + 圆角），两处菜单视觉一致；菜单条目本身保持现状不变（宿主仍是 打开 / 开机自启 / 退出）。",
    "acceptance": [
      "pwsh -File scripts/verify.ps1 全过（build + ctest + selftest + smoke）",
      "build/NeonBoardHost.exe --menu-preview build/menu-host.png 退出码 0（VS 生成器为 build/Release/NeonBoardHost.exe），产出 PNG 大于 5 KB，人工确认圆角、辉光、中文文本与开机自启勾选态正常",
      "build/NeonBoard.exe --menu-preview build/menu-qt.png 退出码 0（VS 生成器为 build/Release/NeonBoard.exe），产出 PNG 大于 5 KB，人工确认与宿主菜单同一套配色与圆角",
      "rg 'CreatePopupMenu|TrackPopupMenu' src/host/HostMain.cpp 无匹配：宿主不再使用系统菜单"
    ],
    "scope": [
      "src/host/**",
      "src/shell/TrayController.*",
      "src/theme/**",
      "src/main.cpp",
      "CMakeLists.txt",
      "tests/**",
      "README.md",
      "docs/iteration-roadmap.md"
    ],
    "depends": []
  }
}
```
