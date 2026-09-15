from __future__ import annotations

from pathlib import Path

import pytest

from auto_short.composer import compose_short
from auto_short.sample_clips import generate_sample_clip
from auto_short.teams import load_catalog


ROOT = Path(__file__).resolve().parents[1]
CONFIG = ROOT / "config" / "teams.yaml"


@pytest.mark.integration
def test_compose_short_from_sample(tmp_path: Path) -> None:
    catalog = load_catalog(CONFIG)
    team = catalog.get("lg")
    clip = generate_sample_clip(
        tmp_path / "clip.mp4",
        team=team,
        duration_sec=2.0,
        label="LG DEMO",
    )
    result = compose_short(
        team,
        catalog,
        [clip],
        highlight_title="테스트 하이라이트",
        output_dir=tmp_path / "out",
        max_duration_sec=5,
    )
    assert result.video_path.exists()
    assert result.duration_sec > 0.5
    assert result.video_path.with_suffix(".json").exists()
