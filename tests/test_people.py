from __future__ import annotations

from pathlib import Path

import pytest

from auto_short.cli import main
from auto_short.people import equal_column_focuses
from auto_short.sample_clips import generate_sample_clip
from auto_short.teams import load_catalog


ROOT = Path(__file__).resolve().parents[1]
CONFIG = ROOT / "config" / "teams.yaml"


def test_equal_column_focuses_four() -> None:
    focuses = equal_column_focuses(4)
    assert len(focuses) == 4
    assert focuses[0].focus_x == pytest.approx(0.125)
    assert focuses[1].focus_x == pytest.approx(0.375)
    assert focuses[2].focus_x == pytest.approx(0.625)
    assert focuses[3].focus_x == pytest.approx(0.875)


@pytest.mark.integration
def test_people_command_equal_mode(tmp_path: Path) -> None:
    catalog = load_catalog(CONFIG)
    team = catalog.get("kt")
    video = generate_sample_clip(
        tmp_path / "group.mp4",
        team=team,
        duration_sec=3.0,
        width=1280,
        height=720,
        label="GROUP",
    )
    out = tmp_path / "out"
    code = main(
        [
            "--config",
            str(CONFIG),
            "people",
            "--team",
            "kt",
            "--video",
            str(video),
            "--count",
            "4",
            "--mode",
            "equal",
            "--out",
            str(out),
            "--work-dir",
            str(tmp_path / "work"),
            "--max-duration",
            "3",
            "--preview",
        ]
    )
    assert code == 0
    assert len(list(out.glob("*.mp4"))) == 4
    assert (tmp_path / "work" / "kt" / "preview.jpg").exists()
