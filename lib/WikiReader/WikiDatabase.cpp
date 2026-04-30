#include "WikiDatabase.h"

#include <Logging.h>

#include <cstring>
#include <string>

namespace {
// Read a uint16_t from the file (little-endian)
bool readU16(FsFile& f, uint16_t& out) {
  uint8_t buf[2];
  if (f.read(buf, 2) != 2) return false;
  out = static_cast<uint16_t>(buf[0]) | (static_cast<uint16_t>(buf[1]) << 8);
  return true;
}

// Read a uint32_t from the file (little-endian)
bool readU32(FsFile& f, uint32_t& out) {
  uint8_t buf[4];
  if (f.read(buf, 4) != 4) return false;
  out = static_cast<uint32_t>(buf[0]) | (static_cast<uint32_t>(buf[1]) << 8) |
        (static_cast<uint32_t>(buf[2]) << 16) | (static_cast<uint32_t>(buf[3]) << 24);
  return true;
}
}  // namespace

WikiDatabase::~WikiDatabase() { close(); }

bool WikiDatabase::open(const char* basePath) {
  close();

  std::string idxPath = std::string(basePath) + "/wiki.idx";
  std::string datPath = std::string(basePath) + "/wiki.dat";

  if (!Storage.openFileForRead("WIKI", idxPath.c_str(), idxFile)) {
    LOG_ERR("WIKI", "Failed to open %s", idxPath.c_str());
    return false;
  }
  indexOpen = true;

  if (!Storage.openFileForRead("WIKI", datPath.c_str(), datFile)) {
    LOG_ERR("WIKI", "Failed to open %s", datPath.c_str());
    idxFile.close();
    indexOpen = false;
    return false;
  }
  dataOpen = true;

  // Read article count (first 4 bytes)
  if (!readU32(idxFile, articleCount)) {
    LOG_ERR("WIKI", "Failed to read article count");
    close();
    return false;
  }

  if (articleCount == 0) {
    LOG_DBG("WIKI", "Database is empty");
    return true;
  }

  // Build coarse skip table
  skipTableSize = (articleCount + SKIP_INTERVAL - 1) / SKIP_INTERVAL;
  skipTable = static_cast<uint32_t*>(malloc(skipTableSize * sizeof(uint32_t)));
  if (!skipTable) {
    LOG_ERR("WIKI", "Failed to allocate skip table (%zu entries)", skipTableSize);
    close();
    return false;
  }

  // Walk the index file, recording the file offset of every SKIP_INTERVAL-th entry
  uint32_t filePos = 4;  // after the articleCount uint32
  uint32_t skipIdx = 0;
  skipTable[skipIdx++] = filePos;

  for (uint32_t i = 0; i < articleCount; i++) {
    // Read title length
    uint16_t titleLen = 0;
    if (!readU16(idxFile, titleLen)) {
      LOG_ERR("WIKI", "Skip table build failed at entry %lu", static_cast<unsigned long>(i));
      close();
      return false;
    }
    // Skip over title bytes + dataOffset(4) + dataLen(4)
    const uint32_t entrySize = static_cast<uint32_t>(titleLen) + 8;
    filePos += 2 + entrySize;

    if (!idxFile.seek(filePos)) {
      LOG_ERR("WIKI", "Seek failed at pos %lu", static_cast<unsigned long>(filePos));
      close();
      return false;
    }

    // Record skip table entry at every SKIP_INTERVAL-th boundary (after entry i)
    if (skipIdx < skipTableSize && (i + 1) % SKIP_INTERVAL == 0) {
      skipTable[skipIdx++] = filePos;
    }
  }

  LOG_DBG("WIKI", "Opened database: %lu articles, skip table: %zu entries",
          static_cast<unsigned long>(articleCount), skipTableSize);
  return true;
}

void WikiDatabase::close() {
  if (indexOpen) {
    idxFile.close();
    indexOpen = false;
  }
  if (dataOpen) {
    datFile.close();
    dataOpen = false;
  }
  if (skipTable) {
    free(skipTable);
    skipTable = nullptr;
  }
  skipTableSize = 0;
  articleCount = 0;
}

bool WikiDatabase::seekToEntry(uint32_t index) const {
  if (!indexOpen || index >= articleCount) return false;

  // Use skip table to find the nearest checkpoint before [index]
  uint32_t skipSlot = index / SKIP_INTERVAL;
  if (skipSlot >= skipTableSize) skipSlot = skipTableSize > 0 ? skipTableSize - 1 : 0;

  uint32_t filePos = skipTable[skipSlot];
  uint32_t startEntry = skipSlot * SKIP_INTERVAL;

  if (!idxFile.seek(filePos)) return false;

  // Linear scan from startEntry to index
  for (uint32_t i = startEntry; i < index; i++) {
    uint16_t titleLen = 0;
    if (!readU16(idxFile, titleLen)) return false;
    // Skip title bytes + dataOffset(4) + dataLen(4)
    filePos += 2 + titleLen + 8;
    if (!idxFile.seek(filePos)) return false;
  }

  return true;
}

bool WikiDatabase::getArticleTitle(uint32_t index, char* buf, size_t maxLen) const {
  if (!indexOpen || index >= articleCount || !buf || maxLen == 0) return false;

  if (!seekToEntry(index)) return false;

  uint16_t titleLen = 0;
  if (!readU16(idxFile, titleLen)) return false;

  const size_t copyLen = (titleLen < maxLen - 1) ? titleLen : maxLen - 1;
  if (idxFile.read(buf, copyLen) != static_cast<int>(copyLen)) return false;
  buf[copyLen] = '\0';

  // Skip any remaining title bytes we didn't read (if truncated)
  if (copyLen < titleLen) {
    const uint32_t skip = titleLen - copyLen;
    if (!idxFile.seek(idxFile.position() + skip)) return false;
  }

  return true;
}

bool WikiDatabase::loadArticleText(uint32_t index, uint8_t* buf, size_t bufSize, size_t& bytesRead) const {
  bytesRead = 0;
  if (!indexOpen || !dataOpen || index >= articleCount || !buf || bufSize == 0) return false;

  if (!seekToEntry(index)) return false;

  uint16_t titleLen = 0;
  if (!readU16(idxFile, titleLen)) return false;

  // Skip title
  if (!idxFile.seek(idxFile.position() + titleLen)) return false;

  uint32_t dataOffset = 0;
  uint32_t dataLen = 0;
  if (!readU32(idxFile, dataOffset)) return false;
  if (!readU32(idxFile, dataLen)) return false;

  if (dataLen == 0) {
    bytesRead = 0;
    return true;
  }

  if (dataLen > MAX_ARTICLE_BYTES) {
    LOG_DBG("WIKI", "Article %lu too large (%lu bytes), truncating", static_cast<unsigned long>(index),
            static_cast<unsigned long>(dataLen));
    dataLen = static_cast<uint32_t>(MAX_ARTICLE_BYTES);
  }

  const size_t readSize = (dataLen < bufSize) ? dataLen : bufSize;

  if (!datFile.seek(dataOffset)) return false;
  const int n = datFile.read(buf, readSize);
  if (n < 0) return false;

  bytesRead = static_cast<size_t>(n);
  return true;
}

int WikiDatabase::compareTitleWithPrefix(uint32_t index, const char* prefix) const {
  char buf[128];
  if (!getArticleTitle(index, buf, sizeof(buf))) return 0;
  const size_t prefixLen = strlen(prefix);
  return strncmp(buf, prefix, prefixLen);
}

int32_t WikiDatabase::findArticleByPrefix(const char* prefix) const {
  if (!indexOpen || articleCount == 0 || !prefix) return -1;

  uint32_t lo = 0;
  uint32_t hi = articleCount;

  while (lo < hi) {
    uint32_t mid = lo + (hi - lo) / 2;
    const int cmp = compareTitleWithPrefix(mid, prefix);
    if (cmp < 0) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }

  return (lo < articleCount) ? static_cast<int32_t>(lo) : static_cast<int32_t>(articleCount - 1);
}
