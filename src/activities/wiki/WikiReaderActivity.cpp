#include "WikiReaderActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WikiDatabase.h>
#include <esp_system.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "WikiArticleActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void WikiReaderActivity::onEnter() {
  Activity::onEnter();

  menuIndex = 0;
  browseMode = false;
  browseIndex = 0;
  browsePage = 0;

  dbAvailable = db.open(SETTINGS.wikiDatabasePath);
  if (!dbAvailable) {
    LOG_ERR("WRA", "Wiki database not found at '%s'", SETTINGS.wikiDatabasePath);
  }

  // Seed PRNG with hardware entropy once per activity entry
  srand(esp_random());

  requestUpdate();
}

void WikiReaderActivity::onExit() {
  Activity::onExit();
  db.close();
}

// ---------------------------------------------------------------------------
// Input handling
// ---------------------------------------------------------------------------

void WikiReaderActivity::loop() {
  // Back always exits to home
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (browseMode) {
      browseMode = false;
      requestUpdate();
    } else {
      onGoHome();
    }
    return;
  }

  if (!dbAvailable) return;

  const int articleCount = static_cast<int>(db.getArticleCount());

  if (browseMode) {
    // Navigate browse list
    buttonNavigator.onNextRelease([this, articleCount] {
      browseIndex = ButtonNavigator::nextIndex(browseIndex, articleCount);
      browsePage = browseIndex / browsePerPage;
      requestUpdate();
    });

    buttonNavigator.onPreviousRelease([this, articleCount] {
      browseIndex = ButtonNavigator::previousIndex(browseIndex, articleCount);
      browsePage = browseIndex / browsePerPage;
      requestUpdate();
    });

    buttonNavigator.onNextContinuous([this, articleCount] {
      browseIndex = ButtonNavigator::nextPageIndex(browseIndex, articleCount, browsePerPage);
      browsePage = browseIndex / browsePerPage;
      requestUpdate();
    });

    buttonNavigator.onPreviousContinuous([this, articleCount] {
      browseIndex = ButtonNavigator::previousPageIndex(browseIndex, articleCount, browsePerPage);
      browsePage = browseIndex / browsePerPage;
      requestUpdate();
    });

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      openArticle(static_cast<uint32_t>(browseIndex));
    }
  } else {
    // Top-level menu navigation
    buttonNavigator.onNextRelease([this] {
      menuIndex = ButtonNavigator::nextIndex(menuIndex, MENU_ITEMS);
      requestUpdate();
    });

    buttonNavigator.onPreviousRelease([this] {
      menuIndex = ButtonNavigator::previousIndex(menuIndex, MENU_ITEMS);
      requestUpdate();
    });

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (menuIndex == 0) {
        openRandomArticle();
      } else {
        browseMode = true;
        browseIndex = 0;
        browsePage = 0;
        requestUpdate();
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Article opening helpers
// ---------------------------------------------------------------------------

void WikiReaderActivity::openRandomArticle() {
  const uint32_t count = db.getArticleCount();
  if (count == 0) return;
  openArticle(static_cast<uint32_t>(rand()) % count);
}

void WikiReaderActivity::openArticle(uint32_t index) {
  startActivityForResult(std::make_unique<WikiArticleActivity>(renderer, mappedInput, db, index),
                         [this](const ActivityResult&) { requestUpdate(); });
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void WikiReaderActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  // Header with article count subtitle
  char subtitle[40] = "";
  if (dbAvailable && db.getArticleCount() > 0) {
    snprintf(subtitle, sizeof(subtitle), tr(STR_WIKI_ARTICLE_COUNT),
             static_cast<unsigned long>(db.getArticleCount()));
  }
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_WIKI_READER),
                 subtitle[0] ? subtitle : nullptr);

  if (!dbAvailable) {
    renderer.drawCenteredText(UI_12_FONT_ID, pageHeight / 2, tr(STR_WIKI_NO_DATABASE), true, EpdFontFamily::REGULAR);
    const auto labels = mappedInput.mapLabels(tr(STR_HOME), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (browseMode) {
    renderBrowseList();
    return;
  }

  // Top-level menu
  const auto menuTop =
      metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const auto menuHeight =
      pageHeight - menuTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  GUI.drawButtonMenu(
      renderer, Rect{0, menuTop, pageWidth, menuHeight}, MENU_ITEMS, menuIndex,
      [](int index) -> std::string {
        if (index == 0) return tr(STR_WIKI_RANDOM_ARTICLE);
        return tr(STR_WIKI_BROWSE_INDEX);
      },
      [](int /*index*/) -> UIIcon { return Book; });

  const auto labels = mappedInput.mapLabels(tr(STR_HOME), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void WikiReaderActivity::renderBrowseList() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  browsePerPage = UITheme::getNumberOfItemsPerPage(renderer, true, false, true, false);
  if (browsePerPage < 1) browsePerPage = 1;

  const int articleCount = static_cast<int>(db.getArticleCount());
  const int pageStart = browsePage * browsePerPage;
  const int visibleItems = std::min(browsePerPage, articleCount - pageStart);
  const int displayIndex = browseIndex - pageStart;

  // Pre-load page titles from SD into heap-allocated buffers to avoid per-draw SD reads
  // (stack limit is 256B; 12×128=1536B requires heap)
  static constexpr int MAX_PER_PAGE = 12;
  static constexpr size_t TITLE_BUF_LEN = 128;
  const int loadItems = (visibleItems < MAX_PER_PAGE) ? visibleItems : MAX_PER_PAGE;

  char* titleMem = static_cast<char*>(malloc(static_cast<size_t>(loadItems) * TITLE_BUF_LEN));
  if (!titleMem) {
    LOG_ERR("WRA", "Failed to allocate title buffer");
    renderer.displayBuffer();
    return;
  }
  memset(titleMem, 0, static_cast<size_t>(loadItems) * TITLE_BUF_LEN);
  for (int i = 0; i < loadItems; i++) {
    db.getArticleTitle(static_cast<uint32_t>(pageStart + i), titleMem + i * TITLE_BUF_LEN, TITLE_BUF_LEN);
  }

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, visibleItems, displayIndex,
      [titleMem, loadItems](int index) -> std::string {
        if (index >= 0 && index < loadItems) {
          return std::string(titleMem + index * TITLE_BUF_LEN);
        }
        return {};
      },
      nullptr, nullptr, nullptr, false);

  free(titleMem);
  titleMem = nullptr;

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
