from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import yaml

DEFAULT_CONFIG = Path(__file__).resolve().parents[2] / "config" / "teams.yaml"


@dataclass(frozen=True)
class Defaults:
    max_duration_sec: float = 40.0
    width: int = 1080
    height: int = 1920
    fps: int = 30
    font: str = "/usr/share/fonts/truetype/nanum/NanumGothicBold.ttf"
    hashtag_prefix: str = "#쇼츠 #Shorts #KBO #야구"


@dataclass(frozen=True)
class Team:
    id: str
    name_ko: str
    name_en: str
    city: str
    primary: str
    secondary: str
    accent: str = "#FFFFFF"
    hashtags: list[str] = field(default_factory=list)

    @property
    def primary_hex(self) -> str:
        return self.primary.lstrip("#")

    @property
    def secondary_hex(self) -> str:
        return self.secondary.lstrip("#")

    @property
    def accent_hex(self) -> str:
        return self.accent.lstrip("#")


@dataclass(frozen=True)
class TeamCatalog:
    defaults: Defaults
    teams: dict[str, Team]

    def get(self, team_id: str) -> Team:
        key = team_id.strip().lower()
        if key not in self.teams:
            known = ", ".join(sorted(self.teams))
            raise KeyError(f"알 수 없는 구단 ID: {team_id!r}. 사용 가능: {known}")
        return self.teams[key]

    def list_ids(self) -> list[str]:
        return sorted(self.teams)


def _parse_defaults(raw: dict[str, Any] | None) -> Defaults:
    raw = raw or {}
    return Defaults(
        max_duration_sec=float(raw.get("max_duration_sec", 40)),
        width=int(raw.get("width", 1080)),
        height=int(raw.get("height", 1920)),
        fps=int(raw.get("fps", 30)),
        font=str(raw.get("font", Defaults.font)),
        hashtag_prefix=str(raw.get("hashtag_prefix", Defaults.hashtag_prefix)),
    )


def _parse_team(team_id: str, raw: dict[str, Any]) -> Team:
    return Team(
        id=str(raw.get("id", team_id)),
        name_ko=str(raw["name_ko"]),
        name_en=str(raw.get("name_en", "")),
        city=str(raw.get("city", "")),
        primary=str(raw["primary"]),
        secondary=str(raw.get("secondary", "#000000")),
        accent=str(raw.get("accent", "#FFFFFF")),
        hashtags=list(raw.get("hashtags") or []),
    )


def load_catalog(path: Path | str | None = None) -> TeamCatalog:
    config_path = Path(path) if path else DEFAULT_CONFIG
    with config_path.open(encoding="utf-8") as fh:
        data = yaml.safe_load(fh) or {}

    defaults = _parse_defaults(data.get("defaults"))
    teams_raw = data.get("teams") or {}
    teams = {team_id: _parse_team(team_id, raw) for team_id, raw in teams_raw.items()}
    if not teams:
        raise ValueError(f"구단 설정이 비어 있습니다: {config_path}")
    return TeamCatalog(defaults=defaults, teams=teams)
