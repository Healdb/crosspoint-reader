# WikiReader Database Format

This document describes the binary database format used by CrossPoint WikiReader
and explains how to create a database from a Wikipedia dump.

## File Layout

Place both files in a directory on your SD card (default path: `/wiki/`).  The
path can be changed in **Settings → WikiReader**.

| File | Purpose |
|------|---------|
| `wiki.idx` | Sorted article index (titles + offsets) |
| `wiki.dat` | Concatenated UTF-8 article bodies |

---

## `wiki.idx` — Index File

All integers are **little-endian**.

```
uint32_t  articleCount          // total number of articles

// Repeated articleCount times, sorted alphabetically by title:
uint16_t  titleLen              // byte length of UTF-8 title (no NUL terminator)
uint8_t   title[titleLen]       // UTF-8 title bytes
uint32_t  dataOffset            // byte offset of article body in wiki.dat
uint32_t  dataLen               // byte length of article body in wiki.dat
```

Articles **must** be sorted alphabetically by title (case-insensitive sort is
recommended, but the firmware performs byte-level comparisons, so a consistent
sort order is what matters).

---

## `wiki.dat` — Data File

Raw UTF-8 text, concatenated with no delimiters or padding.  The `dataOffset`
and `dataLen` fields in the index locate each article's body within this file.

Article text should be plain prose — wikitext markup, HTML tags, and template
expansions should be stripped before inclusion.  The firmware does **not** parse
any markup.

---

## Memory Constraints

The firmware is designed for the ESP32-C3 with ~380 KB usable RAM.

- The index file is **not** loaded into RAM.  All reads are SD seek operations.
- A small coarse skip table (one entry per 128 articles, 4 bytes each) is held
  in DRAM.  For a 200 000-article database that costs ≈ 6 KB.
- Article bodies are loaded one at a time into a heap buffer.  The maximum size
  per article is **32 KB** (`WikiDatabase::MAX_ARTICLE_BYTES`).  Longer articles
  are silently truncated.

---

## Building a Database

Use the included `scripts/make_wiki_db.py` tool:

```bash
# From a Wikipedia XML dump (download from https://dumps.wikimedia.org/)
python3 scripts/make_wiki_db.py enwiki-latest-articles.xml.bz2 /tmp/wiki_out \
    --max-articles 50000 \
    --max-article-bytes 32768

# From a Kiwix ZIM file (see "Using Kiwix ZIM files" section below)
python3 scripts/make_wiki_db.py wikipedia_en_simple_all_nopic.zim /tmp/wiki_out

# From a plain-text dump (articles delimited by "== Title ==" lines)
python3 scripts/make_wiki_db.py articles.txt /tmp/wiki_out

# Copy to SD card
cp /tmp/wiki_out/wiki.idx /media/sdcard/wiki/
cp /tmp/wiki_out/wiki.dat /media/sdcard/wiki/
```

For better wikitext stripping, install `mwparserfromhell`:

```bash
pip install mwparserfromhell
```

Then modify `strip_wikitext()` in the script to use it.

---

## Using Kiwix ZIM Files

[Kiwix](https://www.kiwix.org/) provides ready-to-download ZIM archives of
Wikipedia — including a *text-only* (no pictures) edition that is much smaller
than the full dump and already has images and media stripped out.  This is the
recommended input for CrossPoint.

### 1. Install the Python ZIM library

```bash
pip install libzim
```

### 2. Download a ZIM file

Visit <https://library.kiwix.org/> and filter by:

- **Language**: English (or your preferred language)
- **Search**: `wikipedia_en_simple_all_nopic` (Simple English, no pictures)

The "nopic" (no-pictures) variant is the best fit: it is compact and its
articles contain only prose text.  Typical size is **400–900 MB** compressed.

Direct link example:
```
https://download.kiwix.org/zim/wikipedia/wikipedia_en_simple_all_nopic_YYYY-MM.zim
```

### 3. Convert to CrossPoint format

```bash
python3 scripts/make_wiki_db.py wikipedia_en_simple_all_nopic_2024-10.zim ./wiki_db
```

Optional flags:

```bash
# Limit to first 50 000 articles (faster for testing)
python3 scripts/make_wiki_db.py wikipedia_en_simple_all_nopic_2024-10.zim ./wiki_db \
    --max-articles 50000

# Increase per-article size limit (firmware cap is 32 768 bytes)
python3 scripts/make_wiki_db.py wikipedia_en_simple_all_nopic_2024-10.zim ./wiki_db \
    --max-article-bytes 32768
```

### 4. Copy to SD card

```bash
cp ./wiki_db/wiki.idx /media/sdcard/wiki/
cp ./wiki_db/wiki.dat /media/sdcard/wiki/
```

### What the converter does

1. Opens the ZIM archive and iterates all entries.
2. Skips redirects, images, CSS, JavaScript, and any non-HTML entries.
3. Strips all HTML tags from article content using Python's built-in
   `html.parser` — no extra dependencies beyond `libzim`.
4. Encodes the result as UTF-8, truncates to `--max-article-bytes`, and writes
   it into the CrossPoint binary format.

### Expected output size (Simple English Wikipedia, ~250 000 articles)

| File | Size |
|------|------|
| `wiki.dat` | ~600 MB |
| `wiki.idx` | ~15 MB |
| SD card space needed | ~620 MB |

The skip table held in DRAM will be ~8 KB for 250 000 articles — well within the
380 KB RAM budget.

---

## Recommended Database Sources

| Source | Notes |
|--------|-------|
| [Wikipedia dumps](https://dumps.wikimedia.org/enwiki/latest/) | `enwiki-latest-articles.xml.bz2` — full English Wikipedia |
| [Kiwix ZIM files](https://library.kiwix.org/) | `wikipedia_en_simple_all_nopic` — pre-filtered, no images, recommended |
| [Simple English Wikipedia](https://dumps.wikimedia.org/simplewiki/latest/) | XML dump alternative — smaller vocabulary |
