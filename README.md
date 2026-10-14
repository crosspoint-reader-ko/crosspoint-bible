# CrossPoint Bible

Xteink X4/X3용 성경 전용 펌웨어. [CrossPoint 한국어 펌웨어](https://github.com/crosspoint-reader-ko/crosspoint-reader-ko)(`release/korean`, 1.6.5-ko.1)에서 성경 읽기에 필요 없는 기능을 걷어내고 책·장·절 이동과 큰 글씨를 넣었다.

- 켜면 마지막에 읽던 곳이 열린다.
- 확인 버튼: 책 → 장 → 절 선택. 책은 구약/신약 약어 격자에서 고르고, 장과 절은 번호 격자에서 고른다. 장 화면에서 확인을 길게 누르면 1절로 바로 간다.
- 좌우 버튼과 옆 버튼: 쪽 넘김. 장과 책 경계를 넘어 이어진다. 길게 누르면 장 단위로 이동한다.
- 뒤로 버튼: 메뉴(장절 이동, 글자 크기 14/17/20/24/28pt, 줄 간격, 절마다 줄바꿈/문단, 소제목, 화면 방향, 설정).

계획과 진행은 [docs/bible-plan.md](docs/bible-plan.md)에 있다.

## 성경 넣기

성경 본문은 펌웨어에 들어 있지 않다. 가지고 있는 성경 EPUB을 팩으로 바꿔 SD 카드에 넣는다.

```sh
python3 tools/bible/epub2cpb.py "성경전서 개역개정판.epub" krv.cpb --title 개역개정
```

`krv.cpb`를 SD 카드의 `/bible/` 폴더에 복사한다. 형식은 [docs/bible-pack-format.md](docs/bible-pack-format.md).

## 빌드

```sh
~/.platformio/penv/bin/pio run -e default        # X4/X3 (ESP32-C3)
~/.platformio/penv/bin/pio run -e default -t upload
```

본문 글꼴은 KoPub 바탕 Medium을 KS X 1001 한글(2,350자)로 줄여 압축한 것이다. 다시 만들려면 `lib/EpdFont/scripts/convert-bible-fonts.sh`.

## 테스트

```sh
python3 tools/bible/test_epub2cpb.py                       # 변환기
cmake -S test -B build/test && cmake --build build/test -j8 && ctest --test-dir build/test
BIBLE_CPB=krv.cpb ./build/test/bible_pack/BiblePackTest    # 실제 팩으로 추가 확인
sim/screen-test.sh -b                                       # 에뮬레이터 화면 테스트
```

`sim/screen-test.sh`는 작업 공간의 x4prosim 에뮬레이터(`../x4prosim`)로 펌웨어를 부팅해 `sim/scenarios/*.steps`를 돌리고 `sim/golden/`과 비교한다. SD 카드에 넣을 팩은 `BIBLE_EPUB`(기본: iCloud Drive `Books/_성경`의 EPUB)에서 만든다. 저작권이 있는 본문이므로 팩, 스크린샷, golden(`sim/out/`, `sim/golden/`)은 커밋하지 않는다.

## 원본

CrossPoint Reader(MIT)와 그 한국어 포크를 바탕으로 한다. 원본 README는 git 기록(`0f212bb0`)에 있다.
