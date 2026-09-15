from __future__ import annotations

import json
import shutil
import subprocess
import tempfile
from dataclasses import dataclass
from pathlib import Path

from auto_short.ffmpeg_util import (
    FFmpegNotFound,
    has_drawtext,
    resolve_ffmpeg,
    resolve_ffprobe,
    video_encode_args,
)
from auto_short.metadata import build_description, build_output_stem, build_title
from auto_short.process_util import check_output_cmd, run_cmd
from auto_short.teams import Team, TeamCatalog


@dataclass(frozen=True)
class ComposeResult:
    video_path: Path
    title: str
    description: str
    team_id: str
    duration_sec: float


class ComposeError(RuntimeError):
    pass


def _require_ffmpeg() -> str:
    try:
        return resolve_ffmpeg()
    except FFmpegNotFound as exc:
        raise ComposeError(str(exc)) from exc


def _encode_args() -> list[str]:
    try:
        return video_encode_args(crf=20)
    except FFmpegNotFound as exc:
        raise ComposeError(str(exc)) from exc


def probe_duration(path: Path) -> float:
    try:
        ffprobe = resolve_ffprobe()
    except FFmpegNotFound as exc:
        raise ComposeError(str(exc)) from exc
    cmd = [
        ffprobe,
        "-v",
        "error",
        "-show_entries",
        "format=duration",
        "-of",
        "json",
        str(path),
    ]
    try:
        raw = check_output_cmd(cmd)
    except (subprocess.CalledProcessError, FileNotFoundError) as exc:
        raise ComposeError(f"영상 길이를 읽을 수 없습니다: {path}") from exc
    data = json.loads(raw)
    return float(data["format"]["duration"])


def _escape_drawtext(text: str) -> str:
    # ffmpeg drawtext escaping for text=...
    return (
        text.replace("\\", "\\\\")
        .replace(":", "\\:")
        .replace("'", "\\'")
        .replace("%", "\\%")
    )


def _normalize_clip(
    ffmpeg: str,
    src: Path,
    dst: Path,
    *,
    width: int,
    height: int,
    fps: int,
    max_duration: float,
    focus_x: float | None = None,
    focus_y: float | None = None,
) -> float:
    """Crop to 9:16 (optionally focused on a person) and normalize codec/timebase."""
    duration = min(probe_duration(src), max_duration)
    # Crop in source aspect first, then scale. focus_x/y are 0..1 of source frame.
    fx = 0.5 if focus_x is None else max(0.0, min(1.0, focus_x))
    fy = 0.45 if focus_y is None else max(0.0, min(1.0, focus_y))
    # ow/oh are available inside crop x/y expressions.
    crop_expr = (
        f"crop=min(iw\\,ih*9/16):min(ih\\,iw*16/9):"
        f"max(0\\,min(iw-ow\\,{fx:.6f}*iw-ow/2)):"
        f"max(0\\,min(ih-oh\\,{fy:.6f}*ih-oh/2)),"
        f"scale={width}:{height}:flags=lanczos,fps={fps},setsar=1,format=yuv420p"
    )
    cmd = [
        ffmpeg,
        "-y",
        "-i",
        str(src),
        "-t",
        f"{duration:.3f}",
        "-vf",
        crop_expr,
        "-an",
        *_encode_args(),
        str(dst),
    ]
    _run(cmd, label=f"normalize {src.name}")
    return duration


def _concat_clips(ffmpeg: str, clips: list[Path], dst: Path) -> None:
    list_file = dst.with_suffix(".txt")
    lines = [f"file '{c.resolve().as_posix()}'" for c in clips]
    list_file.write_text("\n".join(lines) + "\n", encoding="utf-8")
    cmd = [
        ffmpeg,
        "-y",
        "-f",
        "concat",
        "-safe",
        "0",
        "-i",
        str(list_file),
        "-c",
        "copy",
        str(dst),
    ]
    try:
        _run(cmd, label="concat clips")
    finally:
        list_file.unlink(missing_ok=True)


def _overlay_branding(
    ffmpeg: str,
    src: Path,
    dst: Path,
    *,
    team: Team,
    title: str,
    font: str,
    width: int,
    height: int,
) -> None:
    team_label = _escape_drawtext(team.name_ko)
    title_label = _escape_drawtext(title)
    city_label = _escape_drawtext(team.city)

    # Top brand bar + bottom caption safe area with team colors.
    vf_parts = [
        f"drawbox=x=0:y=0:w={width}:h=160:color=0x{team.primary_hex}@0.92:t=fill",
        f"drawbox=x=0:y={height - 280}:w={width}:h=280:color=0x{team.secondary_hex}@0.78:t=fill",
        f"drawbox=x=0:y=160:w={width}:h=8:color=0x{team.accent_hex}@0.95:t=fill",
    ]
    if has_drawtext() and Path(font).exists():
        font_path = Path(font)
        vf_parts.extend(
            [
                f"drawtext=fontfile='{font_path.as_posix()}':text='{team_label}':"
                f"fontsize=54:fontcolor=0x{team.accent_hex}:x=(w-text_w)/2:y=48",
                f"drawtext=fontfile='{font_path.as_posix()}':text='{city_label}':"
                f"fontsize=28:fontcolor=0x{team.accent_hex}:x=(w-text_w)/2:y=110",
                f"drawtext=fontfile='{font_path.as_posix()}':text='{title_label}':"
                f"fontsize=42:fontcolor=0x{team.accent_hex}:x=(w-text_w)/2:y=h-180",
            ]
        )
    vf = ",".join(vf_parts)

    cmd = [
        ffmpeg,
        "-y",
        "-i",
        str(src),
        "-vf",
        vf,
        *_encode_args(),
        "-an",
        "-movflags",
        "+faststart",
        str(dst),
    ]
    _run(cmd, label="brand overlay")


def _run(cmd: list[str], *, label: str) -> None:
    proc = run_cmd(cmd)
    if proc.returncode != 0:
        err = (proc.stderr or proc.stdout or "").strip().splitlines()[-20:]
        raise ComposeError(f"{label} 실패\n" + "\n".join(err))


def compose_short(
    team: Team,
    catalog: TeamCatalog,
    clips: list[Path],
    *,
    highlight_title: str,
    output_dir: Path,
    max_duration_sec: float | None = None,
    focus_x: float | None = None,
    focus_y: float | None = None,
) -> ComposeResult:
    if not clips:
        raise ComposeError("클립이 하나 이상 필요합니다.")

    for clip in clips:
        if not clip.exists():
            raise ComposeError(f"클립 파일이 없습니다: {clip}")

    ffmpeg = _require_ffmpeg()
    defaults = catalog.defaults
    max_duration = float(max_duration_sec or defaults.max_duration_sec)
    output_dir.mkdir(parents=True, exist_ok=True)

    display_title = highlight_title.strip() or f"{team.name_ko} 하이라이트"
    yt_title = build_title(team, display_title)
    yt_description = build_description(team, catalog, display_title)
    stem = build_output_stem(team, display_title)
    final_path = output_dir / f"{stem}.mp4"

    with tempfile.TemporaryDirectory(prefix="auto-short-") as tmp:
        tmp_dir = Path(tmp)
        normalized: list[Path] = []
        remaining = max_duration

        for idx, clip in enumerate(clips):
            if remaining <= 0.5:
                break
            out = tmp_dir / f"norm_{idx:02d}.mp4"
            used = _normalize_clip(
                ffmpeg,
                clip,
                out,
                width=defaults.width,
                height=defaults.height,
                fps=defaults.fps,
                max_duration=remaining,
                focus_x=focus_x,
                focus_y=focus_y,
            )
            normalized.append(out)
            remaining -= used

        if not normalized:
            raise ComposeError("유효한 클립이 없습니다.")

        concat_path = tmp_dir / "concat.mp4"
        if len(normalized) == 1:
            shutil.copy2(normalized[0], concat_path)
        else:
            _concat_clips(ffmpeg, normalized, concat_path)

        branded = tmp_dir / "branded.mp4"
        _overlay_branding(
            ffmpeg,
            concat_path,
            branded,
            team=team,
            title=display_title,
            font=defaults.font,
            width=defaults.width,
            height=defaults.height,
        )
        shutil.copy2(branded, final_path)

    duration = probe_duration(final_path)
    meta_path = final_path.with_suffix(".json")
    meta_path.write_text(
        json.dumps(
            {
                "team_id": team.id,
                "team_name": team.name_ko,
                "title": yt_title,
                "description": yt_description,
                "video": str(final_path),
                "duration_sec": duration,
                "source_clips": [str(c) for c in clips],
                "focus_x": focus_x,
                "focus_y": focus_y,
            },
            ensure_ascii=False,
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )

    return ComposeResult(
        video_path=final_path,
        title=yt_title,
        description=yt_description,
        team_id=team.id,
        duration_sec=duration,
    )
