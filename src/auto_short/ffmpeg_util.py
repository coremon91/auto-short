from __future__ import annotations

import os
import shutil
from pathlib import Path


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
