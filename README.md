# CrossPoint Bible

Xteink X4 / X3(ESP32-C3)용 성경 전용 펌웨어. [CrossPoint 한국어 펌웨어](https://github.com/crosspoint-reader-ko/crosspoint-reader-ko)에서 성경 읽기에 필요 없는 기능(네트워크, OTA, 서재, EPUB/TXT 리더 등)을 걷어내고 책·장·절 이동, 큰 글씨, 북마크를 넣었다.

릴리스 펌웨어에는 **개역한글(1952/1961, 공개 저작물)** 이 들어 있다. 다른 번역본은 직접 변환해 SD 카드에 넣는다([성경 바꾸기](#성경-바꾸기)).

| 부팅 | 읽기 | 장절 이동 후 | 절전 |
|---|---|---|---|
| ![부팅 화면](docs/screenshots/splash.png) | ![읽기 화면](docs/screenshots/reader.png) | ![요한복음 3:16으로 이동](docs/screenshots/jump.png) | ![절전 화면](docs/screenshots/sleep.png) |

스크린샷은 x4prosim 에뮬레이터(X3, 528×792)에서 찍었다. 읽기 화면의 본문은 개역개정이다.

## 설치

1. [Releases](https://github.com/crosspoint-reader-ko/crosspoint-bible/releases)에서 `crosspoint-bible-<버전>-x4-x3.bin`을 받는다.
2. CrossPoint 펌웨어가 깔린 기기라면 SD 카드에 복사하고 설정 > 시스템 > SD 카드 펌웨어 업데이트에서 고른다.
3. USB로 올릴 때는 `esptool.py --chip esp32c3 write_flash 0x10000 crosspoint-bible-<버전>-x4-x3.bin`.

이후 갱신도 SD 카드 펌웨어 업데이트로 한다(네트워크 기능이 없다).

## 사용법

### 읽기

- 켜면 마지막에 읽던 곳이 열린다.
- 좌우 버튼, 옆 버튼: 쪽 넘김. 장과 책 경계를 넘어 이어진다. 길게 누르면 장 단위로 이동한다.
- 확인: 장절 이동. 뒤로: 메뉴.
- 처음 설정은 18pt, 줄 간격 보통, 양쪽 정렬, 절마다 줄바꿈이다.

### 장절 이동

책은 구약/신약 약어 격자에서, 장과 절은 번호 격자에서 고른다. 좌우 버튼은 한 칸, 옆 버튼은 한 줄, 옆 버튼을 길게 누르면 한 쪽씩 움직인다. 장 화면에서 확인을 길게 누르면 1절로 바로 간다. 고른 절은 번호에 검은 상자로 표시된다.

| 책 | 장 | 절 |
|---|---|---|
| ![책 선택](docs/screenshots/books.png) | ![장 선택](docs/screenshots/chapters.png) | ![절 선택](docs/screenshots/verses.png) |

### 메뉴와 보기 설정

메뉴: 장절 이동, 북마크 추가, 북마크, 글자 크기(14/16/18/22/28pt), 줄 간격, 절마다 줄바꿈/문단, 소제목, 글자 다듬기, 화면 방향, 성경 정보, 설정.

| 메뉴 | 28pt | 14pt 문단 | 왼쪽 정렬 |
|---|---|---|---|
| ![메뉴](docs/screenshots/menu.png) | ![28pt](docs/screenshots/size-28.png) | ![14pt 문단 보기](docs/screenshots/paragraphs-14.png) | ![왼쪽 정렬](docs/screenshots/align-left.png) |

- 양쪽 정렬은 글자 단위로 줄을 바꾸고, 글자 간격은 그대로 둔 채 남는 폭을 띄어쓰기에만 나눈다. 마침표 같은 문장 부호는 줄 첫머리에 오지 않는다. 정렬은 설정 > 리더에서 바꾼다.
- 글자 다듬기(회색조 안티앨리어싱)를 켜면 쪽을 넘길 때 화면이 두 번 갱신된다. 끄면 한 번이다.

### 북마크

메뉴 > 북마크 추가에서 폴더를 고르거나 새 폴더를 만든다. 폴더 이름은 화면 키보드(두벌식 한글, 영문/숫자)로 입력한다. 저장되는 절은 장절 이동 직후 표시된 절, 아니면 그 쪽의 첫 절이다. 폴더와 북마크는 확인을 길게 눌러 지운다. 파일은 SD 카드 `/.crosspoint/bible-bookmarks.txt`.

| 폴더 고르기 | 새 폴더 이름 | 폴더 | 북마크 | 삭제 |
|---|---|---|---|---|
| ![북마크 추가](docs/screenshots/bookmark-add.png) | ![한글 키보드](docs/screenshots/keyboard.png) | ![폴더 목록](docs/screenshots/bookmark-folders.png) | ![북마크 목록](docs/screenshots/bookmark-items.png) | ![삭제 확인](docs/screenshots/bookmark-delete.png) |

### 성경 정보와 설정

| 내장 개역한글 | 성경 정보 | 설정 > 화면 | 설정 > 리더 |
|---|---|---|---|
| ![개역한글](docs/screenshots/builtin-krv.png) | ![성경 정보](docs/screenshots/info.png) | ![화면 설정](docs/screenshots/settings-display.png) | ![리더 설정](docs/screenshots/settings-reader.png) |

- 성경 정보: 번역본, 판, 저작권 또는 이용 허락, 출처, 버전, 구성(권·장·절·소제목 수), 언어, 위치(내장/SD).
- 절전 화면: 어둡게(표지), 밝게(표지 반전), 없음, 빠른 재개(지금 쪽에 달 표시).
- 설정 > 시스템: 절전 시간, 시계(시계 칩이 있는 기기), SD 카드 펌웨어 업데이트, 기기 정보, 언어.

## 성경 바꾸기

SD 카드 `/bible/`에 `.cpb`가 있으면 내장 성경보다 먼저 쓴다. 여러 개면 이름순 첫 파일이다.

```sh
# EPUB("ESV 템플릿" 한국어 성경)
python3 tools/bible/epub2cpb.py "성경전서 개역개정판.epub" nkrv.cpb --title 개역개정
# getBible v2 JSON (https://api.getbible.net/v2/<번역>.json)
python3 tools/bible/epub2cpb.py korean.json krv1961.cpb --title 개역한글
```

저작권이 있는 번역본(예: 개역개정, © 대한성서공회)은 개인적으로만 쓰고 배포하지 않는다. 형식은 [docs/bible-pack-format.md](docs/bible-pack-format.md), 내장 개역한글의 출처는 [data/bible/README.md](data/bible/README.md).

## 빌드

```sh
~/.platformio/penv/bin/pio run -e default            # 개발 빌드
~/.platformio/penv/bin/pio run -e gh_release         # 릴리스 빌드
~/.platformio/penv/bin/pio run -e default -t upload
```

빌드할 때 `scripts/embed_bible_pack.py`가 내장할 성경을 고른다. `BIBLE_EPUB`(EPUB 또는 JSON), `BIBLE_PACK`(.cpb) 환경 변수가 없으면 `data/bible/krv1961.cpb`다. `BIBLE_EMBED=0`이면 내장하지 않는다.

본문 글꼴은 KoPub 바탕 Medium을 KS X 1001 한글(2,350자)로 줄여 압축한 것이다(`lib/EpdFont/scripts/convert-bible-fonts.sh`). 부팅·절전 표지는 `tools/bible/make_cover.py`가 그린다.

## 테스트

```sh
python3 tools/bible/test_epub2cpb.py
cmake -S test -B build/test && cmake --build build/test -j8
BIBLE_CPB=data/bible/krv1961.cpb ctest --test-dir build/test
sim/screen-test.sh -b      # x4prosim 에뮬레이터 화면 테스트 (작업 공간의 ../x4prosim 필요)
```

계획과 진행은 [docs/bible-plan.md](docs/bible-plan.md).

## 라이선스

MIT([LICENSE](LICENSE)). CrossPoint Reader와 그 한국어 포크를 바탕으로 한다. KoPub 글꼴은 한국출판인회의의 KoPub 서체 이용 조건을 따른다.
