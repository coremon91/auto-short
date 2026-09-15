from __future__ import annotations

from pathlib import Path

import pytest

from auto_short.metadata import build_output_stem, build_title
from auto_short.teams import load_catalog


ROOT = Path(__file__).resolve().parents[1]
CONFIG = ROOT / "config" / "teams.yaml"


def test_load_all_kbo_teams() -> None:
    catalog = load_catalog(CONFIG)
    assert len(catalog.teams) == 10
    assert catalog.get("lg").name_ko == "LG 트윈스"
    assert catalog.get("hanwha").primary.startswith("#")


def test_unknown_team_raises() -> None:
    catalog = load_catalog(CONFIG)
    with pytest.raises(KeyError):
        catalog.get("yankees")


def test_build_title_adds_shorts_tag() -> None:
    catalog = load_catalog(CONFIG)
    team = catalog.get("kia")
    title = build_title(team, "김도영 홈런")
    assert "KIA 타이거즈" in title
    assert "#Shorts" in title


def test_output_stem_is_filesystem_safe() -> None:
    catalog = load_catalog(CONFIG)
    team = catalog.get("lotte")
    stem = build_output_stem(team, "주말 하이라이트!!")
    assert "lotte" in stem
    assert "!" not in stem
