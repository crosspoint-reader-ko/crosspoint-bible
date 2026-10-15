# 내장 성경: 개역한글

`krv1961.cpb`는 펌웨어에 기본으로 들어가는 성경 팩이다.

- 본문: 개역한글판(Korean Revised Version, 1952/1961). 저작권 보호 기간이 끝난 공개 저작물이다.
- 출처: [getBible](https://getbible.net) `korean` 번역(https://api.getbible.net/v2/korean.json), 버전 2.0.1(2019-01-07). getBible은 이 본문을 Wikisource에서 가져와 Public Domain으로 배포한다.
- 원본에 없는 절(마 18:11, 막 9:44 등 17절)은 번호만 있는 빈 절로 둔다. 시편 72:20은 원본 데이터에 없다. 소제목은 없다.

다시 만들기:

```sh
curl -L -o korean.json https://api.getbible.net/v2/korean.json
python3 tools/bible/epub2cpb.py korean.json data/bible/krv1961.cpb --title 개역한글 \
  --meta "edition=개역한글판 (1952/1961)" \
  --meta "note=본문에 없는 절(마 18:11 등)은 빈 절로 둔다. 시편 72:20은 원본 데이터에 없다."
```
