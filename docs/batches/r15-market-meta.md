# R15 批次：插件市场元数据（类别与依赖）立卡清单

授权来源：维护者 2026-11-04 会话。市场仓库 neonboard-market 已上架 8 个插件并手写
`category` / `dependencies`，但两个字段没有单源、客户端也不展示；维护者指示「要做后续的」，
即把这两项落进 NeonBoard 本体。

```json
{
  "R15-01": {
    "stage": "R15",
    "title": "市场元数据单源化：manifest 声明类别与依赖，发布脚本透传",
    "note": "起因：neonboard-market 索引已上架 8 个插件，category/dependencies 是手写进索引的；publish-market.ps1 重建条目只取固定字段，重新发布必丢。把声明收进 plugin.json，发布脚本透传，市场索引由 manifest 单源生成。",
    "acceptance": [
      "pwsh -File scripts/publish-market.ps1 -PluginPath plugins/plan-hook -OutputDir build/market-check 重新打包，build/market-check/market.json 的 plan-hook 条目含 category 与 dependencies 且值与 plugin.json 一致",
      "pwsh -File scripts/verify.ps1 -ScriptOnly 全过"
    ],
    "scope": ["plugins/*/plugin.json", "plugins-samples/*/plugin.json", "scripts/publish-market.ps1", "docs/plugin-api.md"],
    "depends": []
  },
  "R15-02": {
    "stage": "R15",
    "title": "插件库展示类别徽章与依赖说明",
    "note": "起因：市场索引携带 category/dependencies，但 PluginMarket::parseIndex 丢弃未知字段、插件库面板不展示。让类别/依赖贯通 PluginMarket → PluginLibrary.entries → PluginLibraryPanel，市场条目与本地 manifest 一致展示。",
    "acceptance": [
      "pwsh -File scripts/verify.ps1 全过",
      "build/Release/NeonBoard.exe --selftest 退出码 0",
      "pwsh -File scripts/dev-run.ps1 沙箱启动插件库面板：市场条目显示类别徽章与依赖说明，无 QML 报错"
    ],
    "scope": ["src/plugin/**", "qml/components/PluginLibraryPanel.qml", "README.md"],
    "depends": ["R15-01"]
  }
}
```
