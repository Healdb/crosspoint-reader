#pragma once

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * WikiReaderSettingsActivity
 *
 * Dedicated settings page for WikiReader, launched from the main Settings
 * activity.  Follows the same pattern as StatusBarSettingsActivity.
 */
class WikiReaderSettingsActivity final : public Activity {
 public:
  explicit WikiReaderSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("WikiReaderSettings", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;

  void handleSelection();
};
