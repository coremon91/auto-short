from __future__ import annotations

import argparse
import sys
from pathlib import Path

from auto_short.composer import ComposeError, compose_short, probe_duration
from auto_short.cutter import (
    CutError,
    cut_clip,
    detect_scene_clips,
    load_clips_file,
    parse_clip_range,
    split_every,
)
from auto_short.sample_clips import generate_sample_clip
from auto_short.teams import load_catalog
from auto_short.youtube_source import YoutubeError, download_youtube


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
    make_p.add_argument("--out", type=Path, default=Path("output"), help="출력 디렉터리")
    make_p.add_argument(
        "--max-duration",
        type=float,
        default=None,
        help="최대 길이(초). 기본값: 설정 파일 (40초)",
    )
    make_p.add_argument("clips", nargs="+", type=Path, help="원본 하이라이트 클립 경로")
    make_p.set_defaults(func=cmd_make)

    batch_p = sub.add_parser(
        "batch",
        help="clips/<team_id>/*.mp4 구조에서 구단별 쇼츠 일괄 생성",
    )
    batch_p.add_argument("--input-dir", type=Path, required=True)
    batch_p.add_argument("--out", type=Path, default=Path("output"))
    batch_p.add_argument("--title", default="하이라이트 모음")
    batch_p.add_argument("--max-duration", type=float, default=None)
    batch_p.set_defaults(func=cmd_batch)

    demo_p = sub.add_parser("demo", help="더미 클립으로 구단 쇼츠 데모 생성")
    demo_p.add_argument("--team", default=None)
    demo_p.add_argument("--samples-dir", type=Path, default=Path("samples/clips"))
    demo_p.add_argument("--out", type=Path, default=Path("output"))
    demo_p.set_defaults(func=cmd_demo)

    cut_p = sub.add_parser(
        "cut",
        help="긴 원본 영상에서 구간을 잘라 구단 쇼츠로 변환",
    )
    cut_p.add_argument("--team", required=True, help="구단 ID")
    cut_p.add_argument("--video", type=Path, required=True, help="원본 영상 경로")
    cut_p.add_argument("--out", type=Path, default=Path("output"))
    cut_p.add_argument("--work-dir", type=Path, default=Path("samples/cut"))
    cut_p.add_argument(
        "--clip",
        action="append",
        default=[],
        help="구간 START-END 또는 START-END=제목 (여러 번 지정 가능)",
    )
    cut_p.add_argument("--clips-file", type=Path, help="YAML 클립 목록")
    cut_p.add_argument(
        "--split-every",
        type=float,
        default=None,
        help="N초마다 균등 분할 (예: 25)",
    )
    cut_p.add_argument(
        "--scenes",
        action="store_true",
        help="장면 전환 기준으로 자동 분할",
    )
    cut_p.add_argument("--scene-threshold", type=float, default=0.35)
    cut_p.add_argument("--min-clip", type=float, default=4.0)
    cut_p.add_argument("--max-duration", type=float, default=None)
    cut_p.set_defaults(func=cmd_cut)

    yt_p = sub.add_parser(
        "from-youtube",
        help="YouTube 영상을 받아 클립으로 자른 뒤 구단 쇼츠 생성",
    )
    yt_p.add_argument("--url", required=True, help="YouTube URL")
    yt_p.add_argument("--team", required=True, help="구단 ID (이 영상은 kt 추천)")
    yt_p.add_argument("--out", type=Path, default=Path("output"))
    yt_p.add_argument("--work-dir", type=Path, default=Path("samples/youtube"))
    yt_p.add_argument("--cookies", type=Path, help="YouTube cookies.txt 경로")
    yt_p.add_argument(
        "--cookies-from-browser",
        help="브라우저에서 쿠키 읽기 (예: chrome, firefox, edge)",
    )
    yt_p.add_argument(
        "--clip",
        action="append",
        default=[],
        help="구간 START-END 또는 START-END=제목",
    )
    yt_p.add_argument("--clips-file", type=Path, help="YAML 클립 목록")
    yt_p.add_argument("--split-every", type=float, default=None)
    yt_p.add_argument("--scenes", action="store_true", help="장면 전환 자동 분할")
    yt_p.add_argument("--scene-threshold", type=float, default=0.35)
    yt_p.add_argument("--min-clip", type=float, default=4.0)
    yt_p.add_argument("--max-duration", type=float, default=None)
    yt_p.set_defaults(func=cmd_from_youtube)

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
    return [
        p
        for p in sorted(folder.iterdir())
        if p.is_file() and p.suffix.lower() in VIDEO_EXTS
    ]


def cmd_batch(args: argparse.Namespace) -> int:
    catalog = load_catalog(args.config)
    root = args.input_dir
    if not root.is_dir():
        print(f"오류: 입력 디렉터리가 없습니다: {root}", file=sys.stderr)
        return 1

    made = 0
    for team_id in catalog.list_ids():
        clips = _collect_clips(root / team_id)
        if not clips:
            continue
        team = catalog.get(team_id)
        try:
            result = compose_short(
                team,
                catalog,
                clips,
                highlight_title=args.title,
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
            f"생성된 쇼츠가 없습니다. `{root}/<team_id>/*.mp4` 구조를 확인해 주세요.",
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


def _resolve_clip_specs(args: argparse.Namespace, video: Path):
    modes = [
        bool(args.clip),
        bool(args.clips_file),
        args.split_every is not None,
        bool(args.scenes),
    ]
    if sum(1 for m in modes if m) != 1:
        raise CutError(
            "클립 지정은 다음 중 하나만 사용하세요: "
            "--clip / --clips-file / --split-every / --scenes"
        )

    if args.clips_file:
        return load_clips_file(args.clips_file)
    if args.clip:
        return [parse_clip_range(item) for item in args.clip]
    duration = probe_duration(video)
    if args.split_every is not None:
        return split_every(duration, args.split_every)
    return detect_scene_clips(
        video,
        threshold=args.scene_threshold,
        min_len=args.min_clip,
        max_len=float(args.max_duration or 40),
    )


def _render_clip_shorts(args: argparse.Namespace, video: Path) -> int:
    catalog = load_catalog(args.config)
    team = catalog.get(args.team)
    specs = _resolve_clip_specs(args, video)
    if not specs:
        print("오류: 생성된 클립이 없습니다.", file=sys.stderr)
        return 1

    cut_dir = args.work_dir / "clips" / team.id
    cut_dir.mkdir(parents=True, exist_ok=True)
    made = 0

    print(f"원본: {video}")
    print(f"클립 {len(specs)}개 → 쇼츠 생성 ({team.name_ko})")
    for idx, spec in enumerate(specs, start=1):
        raw_path = cut_dir / f"{idx:02d}_{int(spec.start_sec):04d}-{int(spec.end_sec):04d}.mp4"
        print(
            f"  [{idx}/{len(specs)}] {spec.start_sec:.1f}s-{spec.end_sec:.1f}s "
            f"‘{spec.title}’"
        )
        cut_clip(video, raw_path, spec)
        result = compose_short(
            team,
            catalog,
            [raw_path],
            highlight_title=spec.title,
            output_dir=args.out,
            max_duration_sec=args.max_duration,
        )
        print(f"      → {result.video_path.name} ({result.duration_sec:.1f}s)")
        made += 1

    print(f"완료: 쇼츠 {made}개 → {args.out}")
    return 0


def cmd_cut(args: argparse.Namespace) -> int:
    if not args.video.exists():
        print(f"오류: 영상이 없습니다: {args.video}", file=sys.stderr)
        return 1
    try:
        return _render_clip_shorts(args, args.video)
    except (CutError, ComposeError, KeyError) as exc:
        print(f"오류: {exc}", file=sys.stderr)
        return 1


def cmd_from_youtube(args: argparse.Namespace) -> int:
    try:
        dl = download_youtube(
            args.url,
            args.work_dir / "source",
            cookies=args.cookies,
            cookies_from_browser=args.cookies_from_browser,
        )
        print(f"다운로드 완료: {dl.title}")
        print(f"파일: {dl.path}")
        return _render_clip_shorts(args, dl.path)
    except (YoutubeError, CutError, ComposeError, KeyError) as exc:
        print(f"오류: {exc}", file=sys.stderr)
        return 1


def main(argv: list[str] | None = None) -> int:
    args = _parse_args(argv)
    return int(args.func(args))


if __name__ == "__main__":
    raise SystemExit(main())
