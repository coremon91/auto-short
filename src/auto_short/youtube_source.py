from __future__ import annotations

import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


class YoutubeError(RuntimeError):
    pass


@dataclass(frozen=True)
class DownloadResult:
    path: Path
    video_id: str
    title: str


def _yt_dlp_cmd() -> list[str]:
    """Return argv prefix for yt-dlp (binary or python -m)."""
    for name in ("yt-dlp", "yt-dlp.exe"):
        path = shutil.which(name)
        if path:
            return [path]

    # Windows/pip often installs the module but not a PATH entry.
    probe = subprocess.run(
        [sys.executable, "-m", "yt_dlp", "--version"],
        capture_output=True,
        text=True,
    )
    if probe.returncode == 0:
        return [sys.executable, "-m", "yt_dlp"]

    raise YoutubeError(
        "yt-dlp가 필요합니다. PowerShell에서 아래를 실행하세요:\n"
        '  py -m pip install yt-dlp'
    )


def extract_video_id(url: str) -> str:
    patterns = [
        r"(?:youtube\.com/watch\?v=|youtu\.be/|youtube\.com/shorts/)([\w-]{11})",
        r"^([\w-]{11})$",
    ]
    for pattern in patterns:
        match = re.search(pattern, url)
        if match:
            return match.group(1)
    raise YoutubeError(f"YouTube URL/ID를 해석할 수 없습니다: {url}")


def download_youtube(
    url: str,
    output_dir: Path,
    *,
    cookies: Path | None = None,
    cookies_from_browser: str | None = None,
    max_height: int = 1080,
) -> DownloadResult:
    """YouTube 영상을 로컬 파일로 받습니다."""
    yt_dlp = _yt_dlp_cmd()
    video_id = extract_video_id(url)
    output_dir.mkdir(parents=True, exist_ok=True)
    out_tmpl = str(output_dir / f"{video_id}.%(ext)s")

    cmd = [
        *yt_dlp,
        "--no-playlist",
        "--merge-output-format",
        "mp4",
        "-f",
        f"bv*[height<={max_height}]+ba/b[height<={max_height}]/b",
        "-o",
        out_tmpl,
        "--print",
        "after_move:%(filepath)s",
        "--print",
        "after_move:%(title)s",
    ]

    if cookies:
        if not cookies.exists():
            raise YoutubeError(f"쿠키 파일이 없습니다: {cookies}")
        cmd.extend(["--cookies", str(cookies)])
    if cookies_from_browser:
        cmd.extend(["--cookies-from-browser", cookies_from_browser])

    # Prefer node if available for YouTube JS challenges.
    if shutil.which("node"):
        cmd.extend(["--js-runtimes", "node"])

    cmd.append(url)
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        err = (proc.stderr or proc.stdout or "").strip()
        hint = ""
        if "Sign in to confirm" in err or "not a bot" in err:
            hint = (
                "\n\nYouTube 봇 확인에 막혔습니다. 로컬에서 쿠키를 넘겨 주세요.\n"
                "  --cookies cookies.txt\n"
                "  또는 --cookies-from-browser chrome"
            )
        raise YoutubeError(f"다운로드 실패:{hint}\n{err[-2500:]}")

    lines = [ln.strip() for ln in (proc.stdout or "").splitlines() if ln.strip()]
    if len(lines) < 2:
        # Fallback: find downloaded file by id
        matches = sorted(output_dir.glob(f"{video_id}.*"))
        matches = [p for p in matches if p.suffix.lower() in {".mp4", ".mkv", ".webm"}]
        if not matches:
            raise YoutubeError("다운로드된 파일을 찾지 못했습니다.")
        return DownloadResult(path=matches[-1], video_id=video_id, title=video_id)

    path = Path(lines[-2])
    title = lines[-1]
    if not path.exists():
        raise YoutubeError(f"다운로드 경로가 없습니다: {path}")
    return DownloadResult(path=path, video_id=video_id, title=title)
