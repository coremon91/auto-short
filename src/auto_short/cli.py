from __future__ import annotations

import argparse
import sys
from pathlib import Path

from auto_short.composer import ComposeError, compose_short
from auto_short.sample_clips import generate_sample_clip
from auto_short.teams import load_catalog


VIDEO_EXTS = {".mp4", ".mov", ".mkv", ".webm", ".m4v"}


def _parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="auto-short",
        description="KBO 구단별 하이라이트 쇼츠(9:16) 자동 생성기",
    )
    parser.add_argument(
        "--config",
        type=Path,
        default=None,
        help="구단 YAML 설정 경로 (기본: config/teams.yaml)",
    )

    sub = parser.add_subparsers(dest="command", required=True)

    teams_p = sub.add_parser("teams", help="등록된 구단 목록 출력")
    teams_p.set_defaults(func=cmd_teams)

    make_p = sub.add_parser("make", help="한 구단의 쇼츠 생성")
    make_p.add_argument("--team", required=True, help="구단 ID (예: lg, kia, hanwha)")
    make_p.add_argument(
        "--title",
        default="오늘의 하이라이트",
        help="쇼츠 제목/캡션에 들어갈 하이라이트 문구",
    )
    make_p.add_argument(
        "--out",
        type=Path,
        default=Path("output"),
        help="출력 디렉터리",
    )
    make_p.add_argument(
        "--max-duration",
        type=float,
        default=None,
        help="최대 길이(초). 기본값: 설정 파일 (40초)",
    )
    make_p.add_argument(
        "clips",
        nargs="+",
        type=Path,
        help="원본 하이라이트 클립 경로",
    )
    make_p.set_defaults(func=cmd_make)

    batch_p = sub.add_parser(
        "batch",
        help="clips/<team_id>/*.mp4 구조에서 구단별 쇼츠 일괄 생성",
    )
    batch_p.add_argument(
        "--input-dir",
        type=Path,
        required=True,
        help="구단별 하위 폴더가 있는 입력 루트",
    )
    batch_p.add_argument(
        "--out",
        type=Path,
        default=Path("output"),
        help="출력 디렉터리",
    )
    batch_p.add_argument(
        "--title",
        default="하이라이트 모음",
        help="공통 하이라이트 제목",
    )
    batch_p.add_argument(
        "--max-duration",
        type=float,
        default=None,
        help="최대 길이(초)",
    )
    batch_p.set_defaults(func=cmd_batch)

    demo_p = sub.add_parser(
        "demo",
        help="더미 클립으로 전체(또는 지정) 구단 쇼츠 데모 생성",
    )
    demo_p.add_argument(
        "--team",
        default=None,
        help="특정 구단만 생성 (미지정 시 전체)",
    )
    demo_p.add_argument(
        "--samples-dir",
        type=Path,
        default=Path("samples/clips"),
        help="더미 클립 저장 위치",
    )
    demo_p.add_argument(
        "--out",
        type=Path,
        default=Path("output"),
        help="출력 디렉터리",
    )
    demo_p.set_defaults(func=cmd_demo)

    return parser.parse_args(argv)


def cmd_teams(args: argparse.Namespace) -> int:
    catalog = load_catalog(args.config)
    print(f"{'ID':<10} {'구단':<14} {'도시':<6} primary")
    print("-" * 48)
    for team_id in catalog.list_ids():
        team = catalog.get(team_id)
        print(f"{team.id:<10} {team.name_ko:<14} {team.city:<6} {team.primary}")
    return 0


def cmd_make(args: argparse.Namespace) -> int:
    catalog = load_catalog(args.config)
    team = catalog.get(args.team)
    try:
        result = compose_short(
            team,
            catalog,
            list(args.clips),
            highlight_title=args.title,
            output_dir=args.out,
            max_duration_sec=args.max_duration,
        )
    except (ComposeError, KeyError) as exc:
        print(f"오류: {exc}", file=sys.stderr)
        return 1

    print(f"생성 완료: {result.video_path}")
    print(f"제목: {result.title}")
    print(f"길이: {result.duration_sec:.1f}s")
    return 0


def _collect_clips(folder: Path) -> list[Path]:
    if not folder.is_dir():
        return []
    clips = [
        p
        for p in sorted(folder.iterdir())
        if p.is_file() and p.suffix.lower() in VIDEO_EXTS
    ]
    return clips


def cmd_batch(args: argparse.Namespace) -> int:
    catalog = load_catalog(args.config)
    root = args.input_dir
    if not root.is_dir():
        print(f"오류: 입력 디렉터리가 없습니다: {root}", file=sys.stderr)
        return 1

    made = 0
    for team_id in catalog.list_ids():
        team_dir = root / team_id
        clips = _collect_clips(team_dir)
        if not clips:
            continue
        team = catalog.get(team_id)
        title = f"{args.title}"
        try:
            result = compose_short(
                team,
                catalog,
                clips,
                highlight_title=title,
                output_dir=args.out,
                max_duration_sec=args.max_duration,
            )
        except ComposeError as exc:
            print(f"[{team_id}] 실패: {exc}", file=sys.stderr)
            continue
        print(f"[{team_id}] {result.video_path} ({result.duration_sec:.1f}s)")
        made += 1

    if made == 0:
        print(
            "생성된 쇼츠가 없습니다. "
            f"`{root}/<team_id>/*.mp4` 구조를 확인해 주세요.",
            file=sys.stderr,
        )
        return 1
    print(f"총 {made}개 구단 쇼츠 생성")
    return 0


def cmd_demo(args: argparse.Namespace) -> int:
    catalog = load_catalog(args.config)
    team_ids = [args.team] if args.team else catalog.list_ids()
    made = 0

    for team_id in team_ids:
        team = catalog.get(team_id)
        team_dir = args.samples_dir / team.id
        clips = [
            generate_sample_clip(
                team_dir / "play1.mp4",
                team=team,
                duration_sec=2.5,
                label="HIT!",
            ),
            generate_sample_clip(
                team_dir / "play2.mp4",
                team=team,
                duration_sec=2.5,
                label="OUT!",
            ),
        ]
        result = compose_short(
            team,
            catalog,
            clips,
            highlight_title="데모 하이라이트",
            output_dir=args.out,
            max_duration_sec=8,
        )
        print(f"[{team.id}] {result.video_path}")
        made += 1

    print(f"데모 쇼츠 {made}개 생성 완료 → {args.out}")
    return 0


def main(argv: list[str] | None = None) -> int:
    args = _parse_args(argv)
    return int(args.func(args))


if __name__ == "__main__":
    raise SystemExit(main())
