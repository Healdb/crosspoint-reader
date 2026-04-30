#pragma once

#include <WikiDatabase.h>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * WikiReaderActivity
 *
 * Main entry point for the WikiReader feature.  Shows two top-level options
 * (Random Article and Browse A–Z) and handles article selection.
 */
class WikiReaderActivity final : public Activity {
 public:
  explicit WikiReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("WikiReader", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  WikiDatabase db;
  ButtonNavigator buttonNavigator;

  // Top-level menu selection (0 = Random, 1 = Browse)
  int menuIndex = 0;
  static constexpr int MENU_ITEMS = 2;

  // Browse mode state
  bool browseMode = false;
  int browseIndex = 0;   // currently highlighted article
  int browsePage = 0;    // current display page in browse list
  int browsePerPage = 8; // titles per page (updated in render)

  // Whether the database is available
  bool dbAvailable = false;

  void openRandomArticle();
  void openArticle(uint32_t index);
  void renderBrowseList();
};
