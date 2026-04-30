#include "WikiReaderSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int WIKI_MENU_ITEMS = 1;
const StrId wikiMenuNames[WIKI_MENU_ITEMS] = {StrId::STR_WIKI_LONG_PRESS_MODE};
constexpr int WIKI_LONG_PRESS_ITEMS = 2;
const StrId wikiLongPressNames[WIKI_LONG_PRESS_ITEMS] = {StrId::STR_WIKI_ALPHA_MODE, StrId::STR_WIKI_RANDOM_MODE};
}  // namespace

void WikiReaderSettingsActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  requestUpdate();
}

void WikiReaderSettingsActivity::onExit() { Activity::onExit(); }

void WikiReaderSettingsActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    handleSelection();
    requestUpdate();
    return;
  }

  buttonNavigator.onNextRelease([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, WIKI_MENU_ITEMS);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, WIKI_MENU_ITEMS);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, WIKI_MENU_ITEMS);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, WIKI_MENU_ITEMS);
    requestUpdate();
  });
}

void WikiReaderSettingsActivity::handleSelection() {
  if (selectedIndex == 0) {
    SETTINGS.wikiLongPressMode = (SETTINGS.wikiLongPressMode + 1) % WIKI_LONG_PRESS_ITEMS;
  }
  SETTINGS.saveToFile();
}

void WikiReaderSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_CAT_WIKI));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, WIKI_MENU_ITEMS, selectedIndex,
      [](int index) { return std::string(I18N.get(wikiMenuNames[index])); }, nullptr, nullptr,
      [](int index) -> std::string {
        if (index == 0) {
          return I18N.get(wikiLongPressNames[SETTINGS.wikiLongPressMode]);
        }
        return "";
      },
      true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_TOGGLE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
