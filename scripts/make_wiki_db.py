#!/usr/bin/env python3
"""
make_wiki_db.py – Build a CrossPoint WikiReader database from a Wikipedia dump.

Usage
-----
    python3 scripts/make_wiki_db.py INPUT OUTPUT_DIR [--max-articles N] [--max-article-bytes B]

INPUT can be:
  * A Wikipedia XML dump file (*.xml or *.xml.bz2) – full or articles dump
  * A plain-text file where each article is separated by a line that begins
    with "== " (two equals signs and a space followed by the article title).

OUTPUT_DIR will be created if it doesn't exist.  Two files are written:
  OUTPUT_DIR/wiki.idx  – sorted article index
  OUTPUT_DIR/wiki.dat  – concatenated article bodies (UTF-8)

Copy both files to the "/wiki/" directory on your CrossPoint SD card.

Index format (little-endian)
----------------------------
  uint32_t  articleCount
  Per article (sorted alphabetically by title):
    uint16_t  titleLen        byte length of UTF-8 title (no NUL)
    uint8_t   title[titleLen]
    uint32_t  dataOffset      byte offset into wiki.dat
    uint32_t  dataLen         byte length of article body

The articles in wiki.dat are plain UTF-8 text, no delimiters.
"""

import argparse
import os
import struct
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

try:
    import bz2
    HAS_BZ2 = True
except ImportError:
    HAS_BZ2 = False


def strip_wikitext(text: str) -> str:
    """Very lightweight wikitext stripper – removes the most common markup.
    For a production build, consider using mwparserfromhell instead."""
    import re
    # Remove [[File:...]] and [[Image:...]] (multiline-safe)
    text = re.sub(r'\[\[(File|Image)[^\]]*\]\]', '', text, flags=re.IGNORECASE)
    # Remove [[link|display]] → display, [[link]] → link
    text = re.sub(r'\[\[(?:[^|\]]*\|)?([^\]]*)\]\]', r'\1', text)
    # Remove {{templates}}
    text = re.sub(r'\{\{[^}]*\}\}', '', text)
    # Remove HTML tags
    text = re.sub(r'<[^>]+>', '', text)
    # Normalise multiple blank lines
    text = re.sub(r'\n{3,}', '\n\n', text)
    return text.strip()


def parse_xml_dump(path: str, max_articles: int, max_bytes: int):
    """Yield (title, body) pairs from a Wikipedia XML dump."""
    open_fn = bz2.open if path.endswith('.bz2') else open
    ns = 'http://www.mediawiki.org/xml/export-0.10/'

    count = 0
    with open_fn(path, 'rb') as f:
        for event, elem in ET.iterparse(f, events=('end',)):
            if elem.tag != f'{{{ns}}}page':
                continue
            ns_elem = elem.find(f'{{{ns}}}ns')
            if ns_elem is None or ns_elem.text != '0':
                elem.clear()
                continue
            title_elem = elem.find(f'{{{ns}}}title')
            text_elem = elem.find(f'.//{{{ns}}}text')
            if title_elem is None or text_elem is None:
                elem.clear()
                continue
            title = (title_elem.text or '').strip()
            body = strip_wikitext(text_elem.text or '')
            if not title or not body:
                elem.clear()
                continue
            body_bytes = body.encode('utf-8')[:max_bytes]
            yield title, body_bytes
            elem.clear()
            count += 1
            if max_articles and count >= max_articles:
                break


def parse_text_dump(path: str, max_articles: int, max_bytes: int):
    """Yield (title, body) pairs from a simple plain-text dump.
    Articles are delimited by lines starting with '== '."""
    with open(path, 'r', encoding='utf-8', errors='replace') as f:
        current_title = None
        lines = []
        count = 0
        for raw in f:
            line = raw.rstrip('\n')
            if line.startswith('== ') and line.endswith(' =='):
                if current_title and lines:
                    body = '\n'.join(lines).strip().encode('utf-8')[:max_bytes]
                    if body:
                        yield current_title, body
                        count += 1
                        if max_articles and count >= max_articles:
                            return
                current_title = line[3:-3].strip()
                lines = []
            else:
                lines.append(line)
        if current_title and lines:
            body = '\n'.join(lines).strip().encode('utf-8')[:max_bytes]
            if body:
                yield current_title, body


def build_database(articles, output_dir: str):
    """Write wiki.dat and wiki.idx from a sorted list of (title, body_bytes) tuples."""
    Path(output_dir).mkdir(parents=True, exist_ok=True)
    dat_path = os.path.join(output_dir, 'wiki.dat')
    idx_path = os.path.join(output_dir, 'wiki.idx')

    print('Sorting articles…')
    articles.sort(key=lambda x: x[0].lower())

    print(f'Writing {len(articles)} articles to {output_dir}…')
    dat_offset = 0
    index_entries = []

    with open(dat_path, 'wb') as dat:
        for title, body in articles:
            dat.write(body)
            title_bytes = title.encode('utf-8')
            index_entries.append((title_bytes, dat_offset, len(body)))
            dat_offset += len(body)

    with open(idx_path, 'wb') as idx:
        idx.write(struct.pack('<I', len(index_entries)))
        for title_bytes, data_offset, data_len in index_entries:
            idx.write(struct.pack('<H', len(title_bytes)))
            idx.write(title_bytes)
            idx.write(struct.pack('<II', data_offset, data_len))

    print(f'Done.  wiki.dat: {dat_offset} bytes, wiki.idx: {os.path.getsize(idx_path)} bytes')
    print(f'Copy {output_dir}/wiki.idx and {output_dir}/wiki.dat to /wiki/ on your SD card.')


def main():
    parser = argparse.ArgumentParser(description='Build CrossPoint WikiReader database')
    parser.add_argument('input', help='Input file (XML dump or plain text)')
    parser.add_argument('output_dir', help='Output directory for wiki.idx and wiki.dat')
    parser.add_argument('--max-articles', type=int, default=0,
                        help='Limit number of articles (0 = no limit)')
    parser.add_argument('--max-article-bytes', type=int, default=32768,
                        help='Maximum bytes per article body (default: 32768)')
    args = parser.parse_args()

    if not os.path.isfile(args.input):
        print(f'Error: input file not found: {args.input}', file=sys.stderr)
        sys.exit(1)

    if args.input.endswith('.xml') or args.input.endswith('.xml.bz2'):
        if args.input.endswith('.bz2') and not HAS_BZ2:
            print('Error: bz2 module not available', file=sys.stderr)
            sys.exit(1)
        gen = parse_xml_dump(args.input, args.max_articles, args.max_article_bytes)
    else:
        gen = parse_text_dump(args.input, args.max_articles, args.max_article_bytes)

    articles = list(gen)
    print(f'Parsed {len(articles)} articles')
    build_database(articles, args.output_dir)


if __name__ == '__main__':
    main()
