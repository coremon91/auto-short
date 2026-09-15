# auto-short

KBO 구단별 하이라이트 **쇼츠(9:16)** 를 자동으로 만드는 CLI입니다.

원본 클립(또는 YouTube 영상)을 넣으면 구단 컬러 오버레이·제목·메타데이터(JSON)까지 붙여 `1080×1920` Shorts용 mp4를 생성합니다.

## 기능

- KBO 10개 구단 브랜드 설정 (`config/teams.yaml`)
- 가로/세로 원본 → 9:16 센터 크롭 + 정규화
- 구단 컬러 상단 바 / 하단 캡션 오버레이
- YouTube 다운로드 → 구간 자르기 → 쇼츠 일괄 생성
- 장면 전환/`N`초 균등 분할 / YAML 구간 지정
- 기본 최대 길이 **40초**

## 준비

```bash
python3 -m pip install -e ".[dev]"
# ffmpeg, 한글 폰트(Nanum/Noto CJK) 필요
```

## 사용법

### 구단 목록

```bash
auto-short teams
```

### YouTube에서 클립을 따서 쇼츠 만들기

예: [2026 kt위즈 치어리더](https://www.youtube.com/watch?v=sxVqcCslnAE)

**1) 원하는 구간을 직접 지정**

```bash
auto-short from-youtube \
  --url "https://www.youtube.com/watch?v=sxVqcCslnAE" \
  --team kt \
  --clip "0:00-0:25=오프닝" \
  --clip "0:25-0:50=메인1" \
  --clip "0:50-1:15=메인2" \
  --cookies-from-browser chrome
```

**2) YAML로 구간 관리**

`examples/kt-cheerleader-clips.yaml` 시간을 수정한 뒤:

```bash
auto-short from-youtube \
  --url "https://www.youtube.com/watch?v=sxVqcCslnAE" \
  --team kt \
  --clips-file examples/kt-cheerleader-clips.yaml \
  --cookies-from-browser chrome
```

**3) 자동 분할**

```bash
# 25초마다 자르기
auto-short from-youtube --url "..." --team kt --split-every 25 --cookies-from-browser chrome

# 장면 전환 기준
auto-short from-youtube --url "..." --team kt --scenes --cookies-from-browser chrome
```

> YouTube가 봇 확인을 요구하면 `--cookies-from-browser chrome`(또는 `firefox`) /
> `--cookies cookies.txt`가 필요합니다.

### 이미 받은 로컬 영상에서 자르기

```bash
auto-short cut --team kt --video ./source.mp4 --clips-file examples/kt-cheerleader-clips.yaml
```

### 한 구단 쇼츠 만들기 (이미 잘린 클립)

```bash
auto-short make --team kia --title "김도영 홈런" path/to/clip1.mp4 path/to/clip2.mp4
```

### 구단별 폴더 일괄 생성

```text
clips/
  lg/*.mp4
  kia/*.mp4
```

```bash
auto-short batch --input-dir clips --title "어제 하이라이트" --out output
```

### 데모

```bash
auto-short demo --team kt
```

## 구단 ID

| ID | 구단 |
|---|---|
| `lg` | LG 트윈스 |
| `doosan` | 두산 베어스 |
| `kt` | KT 위즈 |
| `ssg` | SSG 랜더스 |
| `nc` | NC 다이노스 |
| `kia` | KIA 타이거즈 |
| `lotte` | 롯데 자이언츠 |
| `samsung` | 삼성 라이온즈 |
| `hanwha` | 한화 이글스 |
| `kiwoom` | 키움 히어로즈 |

## 주의

- 원본 영상의 **저작권·재배포 정책**을 확인한 뒤 사용하세요.
- 클라우드/서버 환경에서는 YouTube 다운로드가 막힐 수 있습니다. 로컬에서 쿠키와 함께 실행하세요.

## 테스트

```bash
pytest -q
```
