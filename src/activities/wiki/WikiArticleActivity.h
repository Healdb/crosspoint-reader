#pragma once

#include <WikiDatabase.h>

#include <cstdint>
#include <vector>

#include "CrossPointSettings.h"
#include "activities/Activity.h"

/**
 * WikiArticleActivity
 *
 * Renders a single Wikipedia article from the wiki database.  Article text is
 * loaded into a heap buffer once on enter, then paginated using the same
 * line-wrap logic as TxtReaderActivity.
 *
 * Long-press PageForward / PageBack navigates to the next/previous article
 * alphabetically (or a random article), according to the wikiLongPressMode
 * setting.
 */
class WikiArticleActivity final : public Activity {
  WikiDatabase& db;

  uint32_t articleIndex = 0;
  bool pendingNavigate = false;
  bool navigateNext = false;

  // Article text (heap-allocated on enter, freed on exit)
  uint8_t* articleBuf = nullptr;
  size_t articleLen = 0;

  int currentPage = 0;
  int totalPages = 1;
  int pagesUntilFullRefresh = 0;

  std::vector<size_t> pageOffsets;
  std::vector<std::string> currentPageLines;

  int linesPerPage = 0;
  int viewportWidth = 0;
  bool initialized = false;

  // Cached layout settings
  int cachedFontId = 0;
  uint8_t cachedScreenMargin = 0;
  uint8_t cachedParagraphAlignment = CrossPointSettings::LEFT_ALIGN;
  int cachedOrientedMarginTop = 0;
  int cachedOrientedMarginRight = 0;
  int cachedOrientedMarginBottom = 0;
  int cachedOrientedMarginLeft = 0;

  void initializeReader();
  void buildPageIndex();
  bool loadPageAtOffset(size_t offset, std::vector<std::string>& outLines, size_t& nextOffset);
  void renderPage();
  void renderStatusBar();
  void saveProgress();
  void loadProgress();
  void navigateArticle(bool next);

 public:
  explicit WikiArticleActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, WikiDatabase& db,
                               uint32_t articleIndex)
      : Activity("WikiArticle", renderer, mappedInput), db(db), articleIndex(articleIndex) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
};
