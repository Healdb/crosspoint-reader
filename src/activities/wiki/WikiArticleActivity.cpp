#include "WikiArticleActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr unsigned long SKIP_ARTICLE_MS = 700;
constexpr char PROGRESS_PATH[] = "/wiki/.progress.bin";
}  // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void WikiArticleActivity::onEnter() {
  Activity::onEnter();

  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);

  // Load article text into heap buffer
  if (!articleBuf) {
    const size_t bufSize = WikiDatabase::MAX_ARTICLE_BYTES + 1;
    articleBuf = static_cast<uint8_t*>(malloc(bufSize));
    if (!articleBuf) {
      LOG_ERR("WAA", "Failed to allocate article buffer");
      requestUpdate();
      return;
    }
    if (!db.loadArticleText(articleIndex, articleBuf, bufSize - 1, articleLen)) {
      LOG_ERR("WAA", "Failed to load article %lu", static_cast<unsigned long>(articleIndex));
      articleLen = 0;
    }
    articleBuf[articleLen] = '\0';
  }

  requestUpdate();
}

void WikiArticleActivity::onExit() {
  Activity::onExit();

  saveProgress();

  if (articleBuf) {
    free(articleBuf);
    articleBuf = nullptr;
  }
  articleLen = 0;

  pageOffsets.clear();
  currentPageLines.clear();
  initialized = false;

  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
}

// ---------------------------------------------------------------------------
// Input handling
// ---------------------------------------------------------------------------

void WikiArticleActivity::loop() {
  // Long press Back → pop back to browse screen
  if (mappedInput.isPressed(MappedInputManager::Button::Back) &&
      mappedInput.getHeldTime() >= ReaderUtils::GO_HOME_MS) {
    finish();
    return;
  }

  // Short press Back → go home
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) &&
      mappedInput.getHeldTime() < ReaderUtils::GO_HOME_MS) {
    onGoHome();
    return;
  }

  auto [prevTriggered, nextTriggered, fromTilt] = ReaderUtils::detectPageTurn(mappedInput);

  // Long press for article navigation (side buttons held > SKIP_ARTICLE_MS)
  if (!fromTilt && SETTINGS.longPressChapterSkip && mappedInput.getHeldTime() > SKIP_ARTICLE_MS) {
    const bool longNext = mappedInput.isPressed(MappedInputManager::Button::PageForward) ||
                          mappedInput.isPressed(MappedInputManager::Button::Right);
    const bool longPrev = mappedInput.isPressed(MappedInputManager::Button::PageBack) ||
                          mappedInput.isPressed(MappedInputManager::Button::Left);
    if (longNext || longPrev) {
      navigateArticle(longNext);
      return;
    }
  }

  if (!prevTriggered && !nextTriggered) {
    return;
  }

  if (prevTriggered) {
    if (currentPage > 0) {
      currentPage--;
      requestUpdate();
    }
  } else if (nextTriggered) {
    if (currentPage < totalPages - 1) {
      currentPage++;
      requestUpdate();
    } else {
      // End of article – go home
      onGoHome();
    }
  }
}

void WikiArticleActivity::navigateArticle(bool next) {
  const uint32_t count = db.getArticleCount();
  if (count == 0) return;

  uint32_t newIndex = articleIndex;
  if (SETTINGS.wikiLongPressMode == CrossPointSettings::WIKI_RANDOM) {
    newIndex = static_cast<uint32_t>(rand()) % count;
  } else {
    // Alphabetical
    if (next) {
      newIndex = (articleIndex + 1 < count) ? articleIndex + 1 : 0;
    } else {
      newIndex = (articleIndex > 0) ? articleIndex - 1 : count - 1;
    }
  }

  if (newIndex == articleIndex) return;

  // Free current article buffer and reset paging state
  if (articleBuf) {
    free(articleBuf);
    articleBuf = nullptr;
  }
  articleLen = 0;
  pageOffsets.clear();
  currentPageLines.clear();
  initialized = false;
  currentPage = 0;
  articleIndex = newIndex;

  // Load new article text
  const size_t bufSize = WikiDatabase::MAX_ARTICLE_BYTES + 1;
  articleBuf = static_cast<uint8_t*>(malloc(bufSize));
  if (!articleBuf) {
    LOG_ERR("WAA", "Failed to allocate article buffer during navigation");
    requestUpdate();
    return;
  }
  if (!db.loadArticleText(articleIndex, articleBuf, bufSize - 1, articleLen)) {
    LOG_ERR("WAA", "Failed to load article %lu", static_cast<unsigned long>(articleIndex));
    articleLen = 0;
  }
  articleBuf[articleLen] = '\0';

  requestUpdate();
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void WikiArticleActivity::render(RenderLock&&) {
  if (!articleBuf) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_MEMORY_ERROR), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  if (!initialized) {
    initializeReader();
  }

  if (pageOffsets.empty()) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_EMPTY_FILE), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  if (currentPage < 0) currentPage = 0;
  if (currentPage >= totalPages) currentPage = totalPages - 1;

  size_t nextOffset;
  currentPageLines.clear();
  loadPageAtOffset(pageOffsets[currentPage], currentPageLines, nextOffset);

  renderer.clearScreen();
  renderPage();
  saveProgress();
}

void WikiArticleActivity::initializeReader() {
  if (initialized) return;

  cachedFontId = SETTINGS.getReaderFontId();
  cachedScreenMargin = SETTINGS.screenMargin;
  cachedParagraphAlignment = SETTINGS.paragraphAlignment;

  renderer.getOrientedViewableTRBL(&cachedOrientedMarginTop, &cachedOrientedMarginRight, &cachedOrientedMarginBottom,
                                   &cachedOrientedMarginLeft);
  cachedOrientedMarginTop += cachedScreenMargin;
  cachedOrientedMarginLeft += cachedScreenMargin;
  cachedOrientedMarginRight += cachedScreenMargin;
  cachedOrientedMarginBottom +=
      std::max(cachedScreenMargin, static_cast<uint8_t>(UITheme::getInstance().getStatusBarHeight()));

  viewportWidth = renderer.getScreenWidth() - cachedOrientedMarginLeft - cachedOrientedMarginRight;
  const int viewportHeight = renderer.getScreenHeight() - cachedOrientedMarginTop - cachedOrientedMarginBottom;
  const int lineHeight = renderer.getLineHeight(cachedFontId);

  linesPerPage = viewportHeight / lineHeight;
  if (linesPerPage < 1) linesPerPage = 1;

  buildPageIndex();
  loadProgress();

  initialized = true;
}

void WikiArticleActivity::buildPageIndex() {
  pageOffsets.clear();
  pageOffsets.push_back(0);

  size_t offset = 0;

  while (offset < articleLen) {
    std::vector<std::string> tempLines;
    size_t nextOffset = offset;

    if (!loadPageAtOffset(offset, tempLines, nextOffset)) break;
    if (nextOffset <= offset) break;

    offset = nextOffset;
    if (offset < articleLen) {
      pageOffsets.push_back(offset);
    }

    if (pageOffsets.size() % 20 == 0) {
      vTaskDelay(1);
    }
  }

  totalPages = static_cast<int>(pageOffsets.size());
  LOG_DBG("WAA", "Article %lu: %d pages", static_cast<unsigned long>(articleIndex), totalPages);
}

bool WikiArticleActivity::loadPageAtOffset(size_t offset, std::vector<std::string>& outLines, size_t& nextOffset) {
  outLines.clear();

  if (offset >= articleLen || !articleBuf) return false;

  const char* src = reinterpret_cast<const char*>(articleBuf) + offset;
  const size_t remaining = articleLen - offset;
  size_t pos = 0;

  while (pos < remaining && static_cast<int>(outLines.size()) < linesPerPage) {
    // Find end of line
    size_t lineEnd = pos;
    while (lineEnd < remaining && src[lineEnd] != '\n') lineEnd++;

    bool lineComplete = (lineEnd < remaining) || (offset + lineEnd >= articleLen);

    if (!lineComplete && !outLines.empty()) break;

    size_t lineContentLen = lineEnd - pos;
    bool hasCR = (lineContentLen > 0 && src[pos + lineContentLen - 1] == '\r');
    size_t displayLen = hasCR ? lineContentLen - 1 : lineContentLen;

    std::string line(src + pos, displayLen);

    size_t lineBytePos = 0;

    while (!line.empty() && static_cast<int>(outLines.size()) < linesPerPage) {
      int lineWidth = renderer.getTextWidth(cachedFontId, line.c_str());

      if (lineWidth <= viewportWidth) {
        outLines.push_back(line);
        lineBytePos = displayLen;
        line.clear();
        break;
      }

      size_t breakPos = line.length();
      while (breakPos > 0 && renderer.getTextWidth(cachedFontId, line.substr(0, breakPos).c_str()) > viewportWidth) {
        size_t spacePos = line.rfind(' ', breakPos - 1);
        if (spacePos != std::string::npos && spacePos > 0) {
          breakPos = spacePos;
        } else {
          breakPos--;
          while (breakPos > 0 && (line[breakPos] & 0xC0) == 0x80) breakPos--;
        }
      }

      if (breakPos == 0) breakPos = 1;

      outLines.push_back(line.substr(0, breakPos));

      size_t skipChars = breakPos;
      if (breakPos < line.length() && line[breakPos] == ' ') skipChars++;
      lineBytePos += skipChars;
      line = line.substr(skipChars);
    }

    if (line.empty()) {
      pos = lineEnd + 1;
    } else {
      pos = pos + lineBytePos;
      break;
    }
  }

  if (pos == 0 && !outLines.empty()) pos = 1;

  nextOffset = offset + pos;
  if (nextOffset > articleLen) nextOffset = articleLen;

  return !outLines.empty();
}

void WikiArticleActivity::renderPage() {
  const int lineHeight = renderer.getLineHeight(cachedFontId);
  const int contentWidth = viewportWidth;

  auto renderLines = [&]() {
    int y = cachedOrientedMarginTop;
    for (const auto& line : currentPageLines) {
      if (!line.empty()) {
        int x = cachedOrientedMarginLeft;

        switch (cachedParagraphAlignment) {
          case CrossPointSettings::LEFT_ALIGN:
          default:
            break;
          case CrossPointSettings::CENTER_ALIGN: {
            int textWidth = renderer.getTextWidth(cachedFontId, line.c_str());
            x = cachedOrientedMarginLeft + (contentWidth - textWidth) / 2;
            break;
          }
          case CrossPointSettings::RIGHT_ALIGN: {
            int textWidth = renderer.getTextWidth(cachedFontId, line.c_str());
            x = cachedOrientedMarginLeft + contentWidth - textWidth;
            break;
          }
          case CrossPointSettings::JUSTIFIED:
          case CrossPointSettings::BOOK_STYLE:
            // Plain text: treat as left-aligned
            break;
        }

        renderer.drawText(cachedFontId, x, y, line.c_str());
      }
      y += lineHeight;
    }
  };

  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  renderLines();
  scope.endScanAndPrewarm();

  renderLines();
  renderStatusBar();

  ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);

  if (SETTINGS.textAntiAliasing) {
    ReaderUtils::renderAntiAliased(renderer, [&renderLines]() { renderLines(); });
  }
}

void WikiArticleActivity::renderStatusBar() {
  const float progress = totalPages > 0 ? (currentPage + 1) * 100.0f / totalPages : 0;
  char title[128] = "";
  if (SETTINGS.statusBarTitle != CrossPointSettings::STATUS_BAR_TITLE::HIDE_TITLE) {
    db.getArticleTitle(articleIndex, title, sizeof(title));
  }
  GUI.drawStatusBar(renderer, progress, currentPage + 1, totalPages, std::string(title));
}

// ---------------------------------------------------------------------------
// Progress persistence
// ---------------------------------------------------------------------------

void WikiArticleActivity::saveProgress() {
  Storage.mkdir("/wiki");
  FsFile f;
  if (Storage.openFileForWrite("WAA", PROGRESS_PATH, f)) {
    uint8_t data[8];
    // article index (4 bytes) + page (4 bytes)
    data[0] = articleIndex & 0xFF;
    data[1] = (articleIndex >> 8) & 0xFF;
    data[2] = (articleIndex >> 16) & 0xFF;
    data[3] = (articleIndex >> 24) & 0xFF;
    data[4] = currentPage & 0xFF;
    data[5] = (currentPage >> 8) & 0xFF;
    data[6] = 0;
    data[7] = 0;
    f.write(data, 8);
    f.close();
  }
}

void WikiArticleActivity::loadProgress() {
  FsFile f;
  if (Storage.openFileForRead("WAA", PROGRESS_PATH, f)) {
    uint8_t data[8];
    if (f.read(data, 8) == 8) {
      uint32_t savedIndex =
          static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
          (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
      int savedPage = data[4] | (data[5] << 8);
      // Only restore page if we're opening the same article
      if (savedIndex == articleIndex && savedPage < totalPages) {
        currentPage = savedPage;
      }
    }
    f.close();
  }
}
