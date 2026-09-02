#pragma once

#include "gd.hpp"

// the ini gets a page inside the vanilla options popup
void applySetting(const char* key, bool value);
// pulls the saved settings values from the games variables and applys them
void syncSettingsFromGame();

void setupOptionsHook();
