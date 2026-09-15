# auto-short

KBO 구단별 하이라이트 **쇼츠(9:16)** 를 자동으로 만드는 CLI입니다.

원본 클립을 넣으면 구단 컬러 오버레이·제목·메타데이터(JSON)까지 붙여 `1080×1920` Shorts용 mp4를 생성합니다.

## 기능

- KBO 10개 구단 브랜드 설정 (`config/teams.yaml`)
- 가로/세로 원본 → 9:16 센터 크롭 + 정규화
- 구단 컬러 상단 바 / 하단 캡션 오버레이
- 기본 최대 길이 **40초** (설정으로 변경 가능)
- 구단별 폴더 일괄 생성 (`batch`)
- 더미 클립 데모 (`demo`)

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

### 한 구단 쇼츠 만들기

```bash
auto-short make --team kia --title "김도영 홈런" path/to/clip1.mp4 path/to/clip2.mp4
```

결과:

- `output/YYYYMMDD_kia_김도영-홈런.mp4`
- 같은 이름 `.json` (YouTube 제목/설명 초안)

### 구단별 폴더 일괄 생성

입력 구조:

```text
clips/
  lg/*.mp4
  doosan/*.mp4
  kia/*.mp4
  ...
```

```bash
auto-short batch --input-dir clips --title "어제 하이라이트" --out output
```

### 데모 (더미 영상)

```bash
auto-short demo
# 또는 한 구단만
auto-short demo --team hanwha
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

색상·해시태그는 `config/teams.yaml`에서 수정합니다.

## 주의

- 공식 중계/하이라이트 영상은 **저작권·중계권 정책**을 확인한 뒤 사용하세요.
- 이 도구는 로컬에 있는 클립을 편집·패키징합니다. 중계 스트림을 무단 수집하지 않습니다.

## 테스트

```bash
pytest -q
```
