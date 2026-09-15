from __future__ import annotations

import re
import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path

import yaml

from auto_short.composer import probe_duration


class CutError(RuntimeError):
    pass


@dataclass(frozen=True)
class ClipSpec:
    start_sec: float
    end_sec: float
    title: str

    @property
    def duration(self) -> float:
        return max(0.0, self.end_sec - self.start_sec)


_TIME_RE = re.compile(
    r"^(?:(?P<h>\d+):)?(?P<m>\d+):(?P<s>\d+(?:\.\d+)?)$|^(?P<sec>\d+(?:\.\d+)?)s?$"
)


def parse_timestamp(value: str | float | int) -> float:
    if isinstance(value, (int, float)):
        return float(value)
    text = str(value).strip()
    match = _TIME_RE.match(text)
    if not match:
        raise CutError(f"시간 형식을 해석할 수 없습니다: {value!r} (예: 12, 0:45, 1:02:03)")
    if match.group("sec") is not None:
        return float(match.group("sec"))
    hours = int(match.group("h") or 0)
    minutes = int(match.group("m") or 0)
    seconds = float(match.group("s") or 0)
    return hours * 3600 + minutes * 60 + seconds


def parse_clip_range(text: str, *, default_title: str | None = None) -> ClipSpec:
    """'START-END' 또는 'START-END=제목' 형식."""
    raw = text.strip()
    title = default_title
    if "=" in raw:
        raw, title = raw.split("=", 1)
        title = title.strip() or title
    if "-" not in raw:
        raise CutError(f"클립 구간은 START-END 형식이어야 합니다: {text!r}")
    start_s, end_s = raw.split("-", 1)
    start = parse_timestamp(start_s)
    end = parse_timestamp(end_s)
    if end <= start:
        raise CutError(f"끝 시간이 시작보다 커야 합니다: {text!r}")
    return ClipSpec(start_sec=start, end_sec=end, title=(title or f"clip_{start:.0f}_{end:.0f}"))


def load_clips_file(path: Path) -> list[ClipSpec]:
    data = yaml.safe_load(path.read_text(encoding="utf-8")) or {}
    items = data.get("clips") if isinstance(data, dict) else data
    if not isinstance(items, list) or not items:
        raise CutError(f"클립 목록이 비어 있거나 잘못되었습니다: {path}")

    clips: list[ClipSpec] = []
    for idx, item in enumerate(items, start=1):
        if isinstance(item, str):
            clips.append(parse_clip_range(item, default_title=f"클립 {idx}"))
            continue
        if not isinstance(item, dict):
            raise CutError(f"클립 항목 형식 오류: {item!r}")
        if "range" in item:
            clips.append(
                parse_clip_range(
                    str(item["range"]),
                    default_title=str(item.get("title") or f"클립 {idx}"),
                )
            )
            continue
        start = parse_timestamp(item["start"])
        end = parse_timestamp(item["end"])
        if end <= start:
            raise CutError(f"끝 시간이 시작보다 커야 합니다: {item}")
        title = str(item.get("title") or f"클립 {idx}")
        clips.append(ClipSpec(start_sec=start, end_sec=end, title=title))
    return clips


def split_every(duration: float, chunk_sec: float, *, max_clips: int = 40) -> list[ClipSpec]:
    if chunk_sec <= 0:
        raise CutError("--split-every 값은 0보다 커야 합니다.")
    clips: list[ClipSpec] = []
    start = 0.0
    idx = 1
    while start < duration - 0.5 and idx <= max_clips:
        end = min(duration, start + chunk_sec)
        clips.append(ClipSpec(start_sec=start, end_sec=end, title=f"클립 {idx}"))
        start = end
        idx += 1
    return clips


def detect_scene_clips(
    video: Path,
    *,
    threshold: float = 0.35,
    min_len: float = 4.0,
    max_len: float = 40.0,
    max_clips: int = 30,
) -> list[ClipSpec]:
    """장면 전환 지점을 기준으로 클립 후보를 만듭니다."""
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        raise CutError("ffmpeg가 필요합니다.")

    duration = probe_duration(video)
    cmd = [
        ffmpeg,
        "-i",
        str(video),
        "-vf",
        f"select='gt(scene,{threshold})',showinfo",
        "-f",
        "null",
        "-",
    ]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    # scene timestamps appear on stderr via showinfo
    times = [0.0]
    for line in (proc.stderr or "").splitlines():
        match = re.search(r"pts_time:(\d+(?:\.\d+)?)", line)
        if match:
            t = float(match.group(1))
            if t - times[-1] >= 0.8:
                times.append(t)
    times.append(duration)

    # Collapse boundaries that are too close (< min_len).
    boundaries = [times[0]]
    for t in times[1:-1]:
        if t - boundaries[-1] >= min_len:
            boundaries.append(t)
    if duration - boundaries[-1] >= min_len * 0.5:
        boundaries.append(duration)
    else:
        boundaries[-1] = duration

    clips: list[ClipSpec] = []
    for i in range(len(boundaries) - 1):
        start = boundaries[i]
        end = boundaries[i + 1]
        cursor = start
        part = 1
        while cursor < end - 0.5 and len(clips) < max_clips:
            chunk_end = min(end, cursor + max_len)
            if chunk_end - cursor < min_len and len(clips) > 0 and chunk_end >= end - 0.01:
                # leftover too short: extend previous clip
                prev = clips[-1]
                clips[-1] = ClipSpec(prev.start_sec, end, prev.title)
                break
            title = f"장면 {len(clips) + 1}"
            if part > 1:
                title = f"{title}-{part}"
            clips.append(ClipSpec(start_sec=cursor, end_sec=chunk_end, title=title))
            cursor = chunk_end
            part += 1
        if len(clips) >= max_clips:
            break
    return clips


def cut_clip(src: Path, dst: Path, clip: ClipSpec) -> Path:
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        raise CutError("ffmpeg가 필요합니다.")
    dst.parent.mkdir(parents=True, exist_ok=True)
    cmd = [
        ffmpeg,
        "-y",
        "-ss",
        f"{clip.start_sec:.3f}",
        "-to",
        f"{clip.end_sec:.3f}",
        "-i",
        str(src),
        "-c:v",
        "libx264",
        "-preset",
        "veryfast",
        "-crf",
        "20",
        "-c:a",
        "aac",
        "-movflags",
        "+faststart",
        str(dst),
    ]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        raise CutError(
            "클립 자르기 실패\n" + "\n".join((proc.stderr or "").strip().splitlines()[-20:])
        )
    return dst
