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

## Recommended Database Sources

| Source | Notes |
|--------|-------|
| [Wikipedia dumps](https://dumps.wikimedia.org/enwiki/latest/) | `enwiki-latest-articles.xml.bz2` — full English Wikipedia |
| [Kiwix ZIM files](https://www.kiwix.org/en/content/) | Pre-filtered subsets (simple English, etc.) — requires separate extraction tooling |
| [Simple English Wikipedia](https://dumps.wikimedia.org/simplewiki/latest/) | Smaller, simpler language — ideal for constrained storage |

### Recommended starting point

Simple English Wikipedia is the best fit for this device:

```bash
wget https://dumps.wikimedia.org/simplewiki/latest/simplewiki-latest-pages-articles.xml.bz2
python3 scripts/make_wiki_db.py simplewiki-latest-pages-articles.xml.bz2 ./wiki_db
```

This produces a database of ~250 000 short articles that comfortably fits on a
typical SD card and renders well on the e-ink display.
