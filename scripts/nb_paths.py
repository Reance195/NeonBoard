# -*- coding: utf-8 -*-
"""Python 侧的 NeonBoard 数据目录单一口径（PowerShell 侧镜像见 scripts/nb-paths.ps1）。

优先读 NEONBOARD_DATA_DIR（沙箱），否则落真实数据目录。
本文件是 Python 侧唯一允许写出数据目录字面量的地方，由
scripts/check_script_hygiene.py 规则 A 强制；其余脚本一律 import 这里。
"""

import os
from pathlib import Path


def data_dir() -> Path:
    env = os.environ.get("NEONBOARD_DATA_DIR")
    if env:
        return Path(env)
    appdata = os.environ.get("APPDATA") or os.path.expanduser("~")
    return Path(appdata) / "com.lin" / "NeonBoard"


def board_db_path() -> Path:
    return data_dir() / "board.db"
