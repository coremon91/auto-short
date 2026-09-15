from __future__ import annotations

from pathlib import Path

import pytest

from auto_short.cli import main
from auto_short.sample_clips import generate_sample_clip
from auto_short.teams import load_catalog


ROOT = Path(__file__).resolve().parents[1]
CONFIG = ROOT / "config" / "teams.yaml"


@pytest.mark.integration
def test_cut_command_split_every(tmp_path: Path) -> None:
    catalog = load_catalog(CONFIG)
    team = catalog.get("kt")
    video = generate_sample_clip(
        tmp_path / "long.mp4",
        team=team,
        duration_sec=9.0,
        label="KT DEMO",
    )
    out = tmp_path / "out"
    work = tmp_path / "work"
    code = main(
        [
            "--config",
            str(CONFIG),
            "cut",
            "--team",
            "kt",
            "--video",
            str(video),
            "--split-every",
            "3",
            "--out",
            str(out),
            "--work-dir",
            str(work),
            "--max-duration",
            "5",
        ]
    )
    assert code == 0
    assert len(list(out.glob("*.mp4"))) == 3
