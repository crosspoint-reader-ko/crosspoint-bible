# 성경 팩

## 개역한글 (`krv1961.cpb`)

한국어 SD 번들(`/bible/lang/ko/krv1961.cpb`)에 들어가는 성경 팩이다.

- 본문: 개역한글판(Korean Revised Version, 1952/1961). 저작권 보호 기간이 끝난 공개 저작물이다.
- 출처: [getBible](https://getbible.net) `korean` 번역(https://api.getbible.net/v2/korean.json), 버전 2.0.1(2019-01-07). getBible은 이 본문을 Wikisource에서 가져와 Public Domain으로 배포한다.
- 원본에 없는 절(마 18:11, 막 9:44 등 17절)은 번호만 있는 빈 절로 둔다. 시편 72:20은 원본 데이터에 없다. 소제목은 없다.

다시 만들기:

```sh
curl -L -o korean.json https://api.getbible.net/v2/korean.json
python3 tools/bible/epub2cpb.py korean.json data/bible/krv1961.cpb --title 개역한글 --lang ko \
  --meta "edition=개역한글판 (1952/1961)" \
  --meta "note=본문에 없는 절(마 18:11 등)은 빈 절로 둔다. 시편 72:20은 원본 데이터에 없다."
```

언어 키(`language_name`, `direction`, `wrap`, `hyphenation`)는 `--lang`에 맞춰 `tools/bible/languages.json`에서 채운다. 본문은 그대로 두고 메타데이터만 다시 쓰려면 팩을 입력으로 준다.

```sh
python3 tools/bible/epub2cpb.py data/bible/krv1961.cpb /tmp/krv1961.cpb --lang ko && mv /tmp/krv1961.cpb data/bible/
```

## Open Bible, 영어 (`otb-en.cpb`)

펌웨어에 내장하는 영어 성경이다.

- 본문: [Open Translation Bible](https://openbible.uk) en-GB, CC BY-SA 4.0. 변환한 팩도 같은 라이선스로 배포한다.
- 출처 커밋은 팩의 `source` 메타데이터에 있다(`Open Translation Bible (https://openbible.uk) en-GB @<commit>`).
- 변환: `---` 구분자는 문단 시작, 시편 표제는 시편 표제 소제목, 그 밖의 구분자(마가복음 16:9, 요한복음 7:53, 8:1 앞의 사본 주석)는 소제목이 된다. 여러 줄로 나뉜 절은 공백 하나로 잇고, 시 행 표시 `> `는 지운다. 시편 78:16 끝에 원본에 섞여 든 번역 도구의 안내문(`[Psalm 78 continues ...]`)은 버린다.

```sh
git clone --depth 1 https://github.com/OpenTranslationBible/open-bible otb
python3 tools/bible/epub2cpb.py otb/lang/en-GB data/bible/otb-en.cpb --lang en
```

다른 언어의 OTB 팩은 `tools/bible/bundle.py`가 SD 번들을 만들 때 같은 방식으로 만든다.
