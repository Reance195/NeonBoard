# NeonBoard

[简体中文](README.md) | English

A cyber-neon three-column personal kanban. **Qt 6.8 + QML + C++20**, fully local, SQLite storage. Single file, offline, instant startup.

![CI](https://github.com/Reance195/NeonBoard/actions/workflows/ci.yml/badge.svg)
![license](https://img.shields.io/badge/license-MIT-blue.svg)

![NeonBoard](docs/screenshot.png)

## Download

| Build | File | Notes |
| --- | --- | --- |
| **Installer (recommended)** | [`NeonBoard-Setup-<ver>.exe`](https://github.com/Reance195/NeonBoard/releases/latest) | Start-menu entry, silent auto-update |
| **Portable** | [`NeonBoard-<ver>-portable.zip`](https://github.com/Reance195/NeonBoard/releases/latest) | Unzip and run, USB-friendly, writes nothing outside its folder |

- Windows 10 1809+ (x64)
- First launch takes ~5–9 s (splash shows immediately); warm start 1.2–1.4 s
- The installer is unsigned — choose *More info → Run anyway* on SmartScreen
- Data lives in `%APPDATA%\com.lin\NeonBoard\board.db` and survives upgrades

### 30-second tour

1. On an empty board, a three-step onboarding can load sample cards
2. Type `tomorrow 15:00 #work !3` and hit Enter — date, tag and priority are parsed live
3. `Ctrl+K` command palette · `Ctrl+Shift+F` focus timer · `Ctrl+Z` universal undo

## What it does

- **Three-column board** (todo / doing / done) with drag-and-drop, precise insertion between cards
- **Natural-language quick entry**: `明天 15:00 #工作 !3` → due date, tags, priority
- **Reminders, overdue glow, recurring tasks** (RFC 5545 RRULE subset)
- **Subtasks, focus timer, heatmap, throughput chart**
- **Universal undo** (delete / move / complete / edit, 50 steps)
- **6 neon themes**, shader-driven background, three quality tiers
- **Plugin platform**: `.neon` single-file packages, multiple market sources, permission model
- **Keyboard-first + screen-reader friendly** (Accessible.name/description/state)
- **Roadmap on the board**: task state derives from `#card-id` commit markers and a ledger, CI writes the snapshot back

## Building from source

Requirements: Qt 6.8.3 (MSVC 2022 x64) + CMake + `qtshadertools`.

```powershell
pwsh -File scripts/build.ps1            # Ninja by default, falls back to MSVC
pwsh -File scripts/verify.ps1           # build → ctest → --selftest → --smoke
```

CI runs the same `verify.ps1` gate on every push, plus a clang-format ratchet and a task-card scope check. See [`AGENTS.md`](AGENTS.md) for the full repository charter (branching, commit format, claim refs, board derivation).

## Plugins

- API reference: [`docs/plugin-api.md`](docs/plugin-api.md)
- Market index: [`Reance195/neonboard-market`](https://github.com/Reance195/neonboard-market)
- Build a package: `pwsh -File scripts/publish-market.ps1 -PluginPath plugins\plan-hook`

## License

MIT. Qt is used under LGPLv3 via dynamic linking.
