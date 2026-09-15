from __future__ import annotations

from pathlib import Path

from auto_short.ffmpeg_util import FFmpegNotFound, resolve_ffmpeg
from auto_short.process_util import run_cmd
from auto_short.teams import Team


def _require_ffmpeg() -> str:
    try:
        return resolve_ffmpeg()
    except FFmpegNotFound as exc:
        raise RuntimeError(str(exc)) from exc


def generate_sample_clip(
    path: Path,
    *,
    team: Team,
    duration_sec: float = 3.0,
    width: int = 1280,
    height: int = 720,
    label: str | None = None,
) -> Path:
    """구단 색상 기반의 더미 하이라이트 클립을 생성합니다."""
    ffmpeg = _require_ffmpeg()
    path.parent.mkdir(parents=True, exist_ok=True)
    text = label or "PLAY"
    # Escape for drawtext
    safe = text.replace(":", "\\:").replace("'", "\\'")
    font = "/usr/share/fonts/truetype/nanum/NanumGothicBold.ttf"
    vf = (
        f"drawbox=x=0:y=0:w={width}:h={height}:color=0x{team.primary_hex}:t=fill,"
        f"drawbox=x=80:y=80:w={width - 160}:h={height - 160}:color=0x{team.secondary_hex}@0.55:t=fill,"
        f"drawtext=fontfile={font}:text='{safe}':fontsize=72:fontcolor=white:"
        f"x=(w-text_w)/2:y=(h-text_h)/2"
    )
    cmd = [
        ffmpeg,
        "-y",
        "-f",
        "lavfi",
        "-i",
        f"color=c=black:s={width}x{height}:d={duration_sec}",
        "-vf",
        vf,
        "-c:v",
        "libx264",
        "-pix_fmt",
        "yuv420p",
        "-t",
        str(duration_sec),
        str(path),
    ]
    proc = run_cmd(cmd)
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr[-2000:])
    return path
