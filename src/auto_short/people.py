from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from auto_short.ffmpeg_util import FFmpegNotFound, resolve_ffmpeg
from auto_short.process_util import run_cmd


class PeopleError(RuntimeError):
    pass


@dataclass(frozen=True)
class PersonFocus:
    index: int  # 1-based left-to-right
    focus_x: float  # 0..1 relative to frame width
    focus_y: float  # 0..1 relative to frame height
    method: str


def _require_cv2():
    try:
        import cv2  # noqa: F401
    except ImportError as exc:
        raise PeopleError(
            "사람 분할에는 opencv가 필요합니다.\n"
            "  py -m pip install opencv-python-headless"
        ) from exc
    import cv2

    return cv2


def extract_frame(video: Path, time_sec: float, out: Path) -> Path:
    try:
        ffmpeg = resolve_ffmpeg()
    except FFmpegNotFound as exc:
        raise PeopleError(str(exc)) from exc
    out.parent.mkdir(parents=True, exist_ok=True)
    cmd = [
        ffmpeg,
        "-y",
        "-ss",
        f"{max(0.0, time_sec):.3f}",
        "-i",
        str(video),
        "-frames:v",
        "1",
        "-q:v",
        "2",
        str(out),
    ]
    proc = run_cmd(cmd)
    if proc.returncode != 0 or not out.exists():
        raise PeopleError("프레임 추출 실패")
    return out


def equal_column_focuses(count: int, *, y: float = 0.42) -> list[PersonFocus]:
    if count < 1:
        raise PeopleError("--count 는 1 이상이어야 합니다.")
    focuses: list[PersonFocus] = []
    for i in range(count):
        # centers of equal-width columns
        focus_x = (i + 0.5) / count
        focuses.append(
            PersonFocus(index=i + 1, focus_x=focus_x, focus_y=y, method="equal")
        )
    return focuses


def detect_face_focuses(
    frame_path: Path,
    *,
    expected_count: int | None = None,
    min_size: int = 40,
) -> list[PersonFocus]:
    cv2 = _require_cv2()
    image = cv2.imread(str(frame_path))
    if image is None:
        raise PeopleError(f"이미지를 열 수 없습니다: {frame_path}")

    height, width = image.shape[:2]
    gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)
    gray = cv2.equalizeHist(gray)

    cascade_path = Path(cv2.data.haarcascades) / "haarcascade_frontalface_default.xml"
    cascade = cv2.CascadeClassifier(str(cascade_path))
    faces = cascade.detectMultiScale(
        gray,
        scaleFactor=1.08,
        minNeighbors=5,
        minSize=(min_size, min_size),
    )

    boxes = sorted([(int(x), int(y), int(w), int(h)) for x, y, w, h in faces], key=lambda b: b[0])
    if expected_count and len(boxes) > expected_count:
        # Keep the expected_count largest faces (still left-to-right ordered).
        boxes = sorted(boxes, key=lambda b: b[2] * b[3], reverse=True)[:expected_count]
        boxes = sorted(boxes, key=lambda b: b[0])

    focuses: list[PersonFocus] = []
    for idx, (x, y, w, h) in enumerate(boxes, start=1):
        cx = (x + w / 2) / width
        # Bias a bit below the face so shoulders/body stay in frame.
        cy = min(0.85, (y + h * 1.8) / height)
        focuses.append(PersonFocus(index=idx, focus_x=cx, focus_y=cy, method="face"))
    return focuses


def resolve_people_focuses(
    video: Path,
    *,
    count: int = 4,
    sample_time: float | None = None,
    mode: str = "auto",
    work_dir: Path,
) -> tuple[list[PersonFocus], Path | None]:
    """
    mode:
      - auto: try face detection, fallback to equal columns
      - face: face detection only
      - equal: equal-width columns (4명이 나란히 있을 때 안정적)
    """
    frame_path = None
    if mode in {"auto", "face"}:
        from auto_short.composer import probe_duration

        duration = probe_duration(video)
        t = sample_time if sample_time is not None else min(max(duration * 0.35, 0.5), max(duration - 0.5, 0.0))
        frame_path = work_dir / "people_sample.jpg"
        extract_frame(video, t, frame_path)
        focuses = detect_face_focuses(frame_path, expected_count=count)
        if mode == "face":
            if len(focuses) < count:
                raise PeopleError(
                    f"얼굴 감지 {len(focuses)}명 / 기대 {count}명. "
                    "--mode equal 또는 --sample-time 을 조정해 보세요."
                )
            return focuses[:count], frame_path
        if len(focuses) >= count:
            return focuses[:count], frame_path
        # fall through to equal

    focuses = equal_column_focuses(count)
    return focuses, frame_path


def draw_debug_overlay(frame_path: Path, focuses: list[PersonFocus], out: Path) -> Path:
    cv2 = _require_cv2()
    image = cv2.imread(str(frame_path))
    if image is None:
        raise PeopleError(f"이미지를 열 수 없습니다: {frame_path}")
    h, w = image.shape[:2]
    for person in focuses:
        x = int(person.focus_x * w)
        y = int(person.focus_y * h)
        cv2.line(image, (x, 0), (x, h), (0, 255, 255), 2)
        cv2.circle(image, (x, y), 12, (0, 0, 255), -1)
        cv2.putText(
            image,
            f"P{person.index}",
            (x + 8, y - 8),
            cv2.FONT_HERSHEY_SIMPLEX,
            1.0,
            (255, 255, 255),
            2,
        )
        # approximate 9:16 crop width on this frame
        crop_w = int(min(w, h * 9 / 16))
        x1 = max(0, x - crop_w // 2)
        x2 = min(w, x1 + crop_w)
        cv2.rectangle(image, (x1, 0), (x2, h), (255, 128, 0), 2)
    out.parent.mkdir(parents=True, exist_ok=True)
    cv2.imwrite(str(out), image)
    return out
