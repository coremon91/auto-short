from __future__ import annotations

from pathlib import Path

from auto_short.cutter import load_clips_file, parse_clip_range, parse_timestamp, split_every
from auto_short.youtube_source import _yt_dlp_cmd, extract_video_id


def test_parse_timestamp_formats() -> None:
    assert parse_timestamp("12") == 12
    assert parse_timestamp("1:02") == 62
    assert parse_timestamp("1:02:03") == 3723
    assert parse_timestamp(3.5) == 3.5


def test_parse_clip_range_with_title() -> None:
    clip = parse_clip_range("0:10-0:35=오프닝")
    assert clip.start_sec == 10
    assert clip.end_sec == 35
    assert clip.title == "오프닝"


def test_split_every() -> None:
    clips = split_every(70, 30)
    assert len(clips) == 3
    assert clips[0].end_sec == 30
    assert clips[-1].end_sec == 70


def test_load_clips_file(tmp_path: Path) -> None:
    path = tmp_path / "clips.yaml"
    path.write_text(
        """
clips:
  - start: "0:00"
    end: "0:20"
    title: A
  - range: "0:20-0:40=B"
""",
        encoding="utf-8",
    )
    clips = load_clips_file(path)
    assert [c.title for c in clips] == ["A", "B"]
    assert clips[1].start_sec == 20


def test_extract_video_id() -> None:
    assert extract_video_id("https://www.youtube.com/watch?v=sxVqcCslnAE") == "sxVqcCslnAE"
    assert extract_video_id("https://youtu.be/sxVqcCslnAE") == "sxVqcCslnAE"


def test_yt_dlp_cmd_resolves() -> None:
    cmd = _yt_dlp_cmd()
    assert cmd
    assert "yt-dlp" in cmd[0] or cmd[-1] == "yt_dlp"
