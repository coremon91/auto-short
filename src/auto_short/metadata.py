from __future__ import annotations

import re
from datetime import date

from auto_short.teams import Team, TeamCatalog


def build_title(team: Team, highlight_title: str) -> str:
    title = f"{team.name_ko} | {highlight_title}".strip()
    if len(title) > 90:
        title = title[:87] + "..."
    if "#Shorts" not in title and "#쇼츠" not in title:
        title = f"{title} #Shorts"
    return title


def build_description(
    team: Team,
    catalog: TeamCatalog,
    highlight_title: str,
    *,
    game_date: date | None = None,
) -> str:
    when = game_date.isoformat() if game_date else date.today().isoformat()
    tags = " ".join([catalog.defaults.hashtag_prefix, *team.hashtags])
    lines = [
        f"{team.name_ko} 하이라이트",
        highlight_title,
        f"경기일: {when}",
        "",
        tags,
        "",
        "※ 공식 중계권/저작권 정책을 확인한 클립만 사용하세요.",
    ]
    return "\n".join(lines)


def build_output_stem(team: Team, highlight_title: str, game_date: date | None = None) -> str:
    when = (game_date or date.today()).strftime("%Y%m%d")
    slug = _slugify(highlight_title) or "highlight"
    return f"{when}_{team.id}_{slug}"


def _slugify(text: str) -> str:
    text = text.strip().lower()
    text = re.sub(r"\s+", "-", text)
    text = re.sub(r"[^0-9a-zA-Z가-힣_-]+", "", text)
    return text[:48].strip("-_")
