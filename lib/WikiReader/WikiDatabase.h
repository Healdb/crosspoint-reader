#pragma once

#include <HalStorage.h>

#include <cstddef>
#include <cstdint>
#include <string>

/**
 * WikiDatabase
 *
 * Reads a pre-built Wikipedia database from the SD card.
 *
 * File format
 * -----------
 * wiki.idx  – sorted article index
 *   uint32_t  articleCount
 *   Per entry (sorted alphabetically by title):
 *     uint16_t  titleLen        (byte length of UTF-8 title, no NUL)
 *     uint8_t   title[titleLen]
 *     uint32_t  dataOffset      (byte offset into wiki.dat)
 *     uint32_t  dataLen         (byte length of article body)
 *
 * wiki.dat  – concatenated UTF-8 article text, no delimiters
 *
 * Memory design
 * -------------
 * The index is NOT loaded into RAM.  All reads are seek-based SD operations
 * so that the full database can be arbitrarily large without consuming DRAM.
 * Only the per-entry metadata (6 bytes: dataOffset + dataLen) for a requested
 * article is kept transiently on the stack during a read.
 *
 * The index header (uint32_t articleCount + the per-entry byte offsets
 * within wiki.idx) IS cached in a small heap array so that binary search
 * can jump directly to any entry without scanning from the beginning.
 * Each entry's index offset = 4 + sum of preceding entry sizes.
 * Because entry sizes vary (title length), we store the file offset of
 * every entry header in an uint32_t[] array.  For a database with 200 000
 * articles that array costs 800 KB — too large.  Instead we store only a
 * coarse skip table (every SKIP_INTERVAL-th entry), and do a short linear
 * scan within each interval.  SKIP_INTERVAL=128 with 200k articles → 1563
 * entries × 4 bytes = 6 KB DRAM, acceptable.
 */
class WikiDatabase {
 public:
  static constexpr size_t SKIP_INTERVAL = 128;
  static constexpr size_t MAX_ARTICLE_BYTES = 32 * 1024;  // 32 KB hard limit per article

  WikiDatabase() = default;
  ~WikiDatabase();

  /**
   * Open the database located at basePath (e.g. "/wiki").
   * Reads the article count and builds the coarse skip table.
   * Returns false if the database files are absent or malformed.
   */
  bool open(const char* basePath);

  /** Close open file handles and free the skip table. */
  void close();

  [[nodiscard]] bool isOpen() const { return indexOpen; }
  [[nodiscard]] uint32_t getArticleCount() const { return articleCount; }

  /**
   * Copy the UTF-8 title of article [index] into buf (NUL-terminated).
   * Returns false on I/O error or if index is out of range.
   */
  bool getArticleTitle(uint32_t index, char* buf, size_t maxLen) const;

  /**
   * Load the body of article [index] into buf.
   * bytesRead receives the number of bytes written (may be truncated to
   * min(dataLen, bufSize)).  Returns false on I/O error.
   */
  bool loadArticleText(uint32_t index, uint8_t* buf, size_t bufSize, size_t& bytesRead) const;

  /**
   * Binary search for the first article whose title is >= prefix.
   * Returns the article index, or -1 if the database is empty / not open.
   */
  int32_t findArticleByPrefix(const char* prefix) const;

 private:
  // Seek the index file to the start of entry [index] and read its header.
  // Returns false on error.
  bool seekToEntry(uint32_t index) const;

  // Read the metadata (dataOffset, dataLen) for entry [index].
  // The index file must already be seeked to that entry (after title bytes).
  bool readEntryMeta(uint32_t& dataOffset, uint32_t& dataLen) const;

  // Compare the title of entry [index] with prefix.
  // Returns negative / zero / positive like strcmp.
  int compareTitleWithPrefix(uint32_t index, const char* prefix) const;

  // File handles – mutable so const methods can seek/read
  mutable FsFile idxFile;
  mutable FsFile datFile;

  bool indexOpen = false;
  bool dataOpen = false;

  uint32_t articleCount = 0;

  // Coarse skip table: skipTable[i] = byte offset in wiki.idx of entry [i * SKIP_INTERVAL]
  uint32_t* skipTable = nullptr;
  size_t skipTableSize = 0;
};
