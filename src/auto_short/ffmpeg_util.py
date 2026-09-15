from __future__ import annotations

import os
import shutil
from functools import lru_cache
from pathlib import Path

from auto_short.process_util import run_cmd


class FFmpegNotFound(RuntimeError):
    pass


_ENV_KEYS = ("AUTO_SHORT_FFMPEG", "FFMPEG", "FFMPEG_PATH")


def _candidate_dirs() -> list[Path]:
    dirs: list[Path] = []
    for key in _ENV_KEYS:
        raw = os.environ.get(key)
        if not raw:
            continue
        p = Path(raw)
        if p.is_file():
            dirs.append(p.parent)
        else:
            dirs.append(p)
            dirs.append(p / "bin")

    # Common local installs (Windows)
    dirs.extend(
        [
            Path(r"Z:\4Kplayer\AutoIngest\FFmpeg"),
            Path(r"Z:\4Kplayer\AutoIngest\FFmpeg\bin"),
            Path(r"C:\ffmpeg\bin"),
            Path(r"C:\Program Files\ffmpeg\bin"),
        ]
    )
    return dirs


def resolve_ffmpeg() -> str:
    found = shutil.which("ffmpeg")
    if found:
        return found

    for directory in _candidate_dirs():
        for name in ("ffmpeg.exe", "ffmpeg"):
            candidate = directory / name
            if candidate.is_file():
                return str(candidate)

    raise FFmpegNotFound(
        "ffmpeg가 필요합니다.\n"
        "PowerShell 예시:\n"
        '  $env:Path = "Z:\\4Kplayer\\AutoIngest\\FFmpeg;" + $env:Path\n'
        "또는:\n"
        '  $env:AUTO_SHORT_FFMPEG = "Z:\\4Kplayer\\AutoIngest\\FFmpeg"'
    )


def resolve_ffprobe() -> str:
    found = shutil.which("ffprobe")
    if found:
        return found

    ffmpeg = None
    try:
        ffmpeg = resolve_ffmpeg()
    except FFmpegNotFound:
        ffmpeg = None

    if ffmpeg:
        sibling = Path(ffmpeg).with_name(
            "ffprobe.exe" if Path(ffmpeg).suffix.lower() == ".exe" else "ffprobe"
        )
        if sibling.is_file():
            return str(sibling)

    for directory in _candidate_dirs():
        for name in ("ffprobe.exe", "ffprobe"):
            candidate = directory / name
            if candidate.is_file():
                return str(candidate)

    raise FFmpegNotFound("ffprobe가 필요합니다. ffmpeg와 같은 폴더에 있어야 합니다.")


@lru_cache(maxsize=1)
def list_encoders() -> frozenset[str]:
    ffmpeg = resolve_ffmpeg()
    proc = run_cmd([ffmpeg, "-hide_banner", "-encoders"])
    names: set[str] = set()
    for line in (proc.stdout or "").splitlines():
        # e.g. " V..... libx264"
        parts = line.strip().split()
        if len(parts) >= 2 and parts[0].startswith("V"):
            names.add(parts[1])
    return frozenset(names)


@lru_cache(maxsize=1)
def list_filters() -> frozenset[str]:
    ffmpeg = resolve_ffmpeg()
    proc = run_cmd([ffmpeg, "-hide_banner", "-filters"])
    names: set[str] = set()
    for line in (proc.stdout or "").splitlines():
        parts = line.strip().split()
        if len(parts) >= 2 and "." in parts[0]:
            names.add(parts[1])
    return frozenset(names)


def video_encode_args(*, crf: int = 20) -> list[str]:
    """Pick encode flags compatible with the installed ffmpeg build."""
    encoders = list_encoders()

    if "libx264" in encoders:
        return [
            "-c:v",
            "libx264",
            "-preset",
            "veryfast",
            "-crf",
            str(crf),
            "-pix_fmt",
            "yuv420p",
        ]
    if "h264_nvenc" in encoders:
        return ["-c:v", "h264_nvenc", "-preset", "p4", "-cq", str(crf), "-pix_fmt", "yuv420p"]
    if "h264_amf" in encoders:
        return ["-c:v", "h264_amf", "-quality", "speed", "-rc", "cqp", "-qp_i", str(crf)]
    if "h264_qsv" in encoders:
        return ["-c:v", "h264_qsv", "-global_quality", str(crf)]
    if "h264_mf" in encoders:
        return ["-c:v", "h264_mf", "-pix_fmt", "nv12"]
    if "libopenh264" in encoders:
        return ["-c:v", "libopenh264", "-pix_fmt", "yuv420p"]
    if "mpeg4" in encoders:
        return ["-c:v", "mpeg4", "-q:v", "5"]

    available = ", ".join(sorted(e for e in encoders if "264" in e or "mpeg" in e)[:20])
    raise FFmpegNotFound(
        "사용 가능한 H.264/MPEG 인코더가 없습니다.\n"
        "풀버전 FFmpeg(essentials) 설치를 권장합니다.\n"
        f"감지된 관련 인코더: {available or '(없음)'}"
    )


def has_drawtext() -> bool:
    return "drawtext" in list_filters()
