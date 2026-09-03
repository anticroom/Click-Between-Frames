#include "options.hpp"
#include "settings.hpp"
#include "log.hpp"

#include <cstring>

#include <MinHook.h>

struct Option {
	const char* gameVariable;
	const char* key;
	const char* name;
	const char* description;
	bool defaultValue;
};

static const Option OPTIONS[] = {
	{ "9001", "soft-toggle", "Disable CBF",
		"Turns <cg>Click Between Frames</c> off.", false },
	{ "9002", "safe-mode", "Safe Mode",
		"Disable progress and stats while CBF is active.", false },
	{ "9003", "click-on-steps", "Click on Steps",
		"Only register inputs on physics steps. Should have identical physics to vanilla.", false },
	{ "9004", "mouse-fix", "Reduce Mouse Lag",
		"Reduce lag when using high polling rate mice.\n<cy>Experimental, may break things.</c>", false },
	{ "9005", "late-cutoff", "Late Cutoff",
		"Check for inputs at the latest possible moment instead of at the start of the frame.", false },
	{ "9006", "right-click", "Right Click P2",
		"Use right click for player 2 jump.", false },
	{ "9007", "thread-priority", "CBF Thread Priority",
		"Run the input thread at a higher priority.\n<cy>Takes effect after a restart.</c>", false },
	{ "9008", "wine-workaround", "CBF Wine Workaround",
		"Read input devices directly when running under Wine.\n<cy>Takes effect after a restart.</c>", true }
};

constexpr int OPTION_COUNT = sizeof(OPTIONS) / sizeof(OPTIONS[0]);
constexpr int TOGGLES_PER_PAGE = 10; // what MoreOptionsLayer::addToggle paginates at

static bool (__thiscall* GameManager_getGameVariable)(GameManager*, const char*) =
	reinterpret_cast<bool(__thiscall*)(GameManager*, const char*)>(0);
static void (__thiscall* GameManager_setGameVariable)(GameManager*, const char*, bool);
static const char* SEED_MARKER = "9000";

static bool seeding = false;

// keep the ini and the running mod in step with whatever the toggle wrote
static void __fastcall GameManager_setGameVariable_H(GameManager* self, void*, const char* key, bool value) {
	GameManager_setGameVariable(self, key, value);
	if (seeding) return;

	for (int i = 0; i < OPTION_COUNT; i++) {
		if (strcmp(OPTIONS[i].gameVariable, key) != 0) continue;
		settings::setBool(OPTIONS[i].key, value);
		settings::save();
		applySetting(OPTIONS[i].key, value);
		return;
	}
}

static void (__thiscall* MoreOptionsLayer_addToggle)(void*, const char*, const char*, const char*);

// number of toggles the layer has added so far, which is what decides the page they land on
static int& toggleCount(void* layer) {
	return *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(layer) + 0x1D8);
}

static bool injectPending = false;

void syncSettingsFromGame() {
	static bool synced = false;
	if (synced) return;
	synced = true;
	GameManager* gm = GameManager::sharedState();

	seeding = true;
	if (!GameManager_getGameVariable(gm, SEED_MARKER)) {
		for (int i = 0; i < OPTION_COUNT; i++) {
			GameManager_setGameVariable(gm, OPTIONS[i].gameVariable, settings::getBool(OPTIONS[i].key, OPTIONS[i].defaultValue));
		}
		GameManager_setGameVariable(gm, SEED_MARKER, true);
	}
	else {
		for (int i = 0; i < OPTION_COUNT; i++) {
			bool value = GameManager_getGameVariable(gm, OPTIONS[i].gameVariable);
			settings::setBool(OPTIONS[i].key, value);
			applySetting(OPTIONS[i].key, value);
		}
		settings::save();
	}
	seeding = false;
}
static void injectToggles(void* layer) {
	syncSettingsFromGame();
	for (int i = 0; i < OPTION_COUNT; i++) {
		MoreOptionsLayer_addToggle(layer, OPTIONS[i].name, OPTIONS[i].gameVariable, OPTIONS[i].description);
	}

	// pad up to a page boundary so the vanilla toggles start on a page of their own
	int& count = toggleCount(layer);
	count = ((count + TOGGLES_PER_PAGE - 1) / TOGGLES_PER_PAGE) * TOGGLES_PER_PAGE;
}

static void __fastcall MoreOptionsLayer_addToggle_H(void* self, void*, const char* name, const char* key, const char* description) {
	if (injectPending) { // put our page in front of the first vanilla toggle
		injectPending = false;
		injectToggles(self);
	}

	MoreOptionsLayer_addToggle(self, name, key, description);
}

static bool (__thiscall* MoreOptionsLayer_init)(void*);
static bool __fastcall MoreOptionsLayer_init_H(void* self, void*) {
	injectPending = true;
	bool result = MoreOptionsLayer_init(self);
	if (injectPending) cbf::log::error("Uh oh! MoreOptionsLayer failed to add any CBF settings in options.");
	injectPending = false;

	return result;
}

static void hook(uintptr_t address, void* detour, void* trampoline, const char* name) {
	MH_STATUS status = MH_CreateHook(reinterpret_cast<void*>(address), detour, reinterpret_cast<void**>(trampoline));
	if (status != MH_OK) cbf::log::error("Failed to hook %s: %s", name, MH_StatusToString(status));
}

void setupOptionsHook() {
	const uintptr_t base = gdBase();

	GameManager_getGameVariable = reinterpret_cast<bool(__thiscall*)(GameManager*, const char*)>(base + 0xC9D30);

	hook(base + 0xC9B50, &GameManager_setGameVariable_H, &GameManager_setGameVariable, "GameManager::setGameVariable");
	hook(base + 0x1DE8F0, &MoreOptionsLayer_init_H, &MoreOptionsLayer_init, "MoreOptionsLayer::init");
	hook(base + 0x1DF6B0, &MoreOptionsLayer_addToggle_H, &MoreOptionsLayer_addToggle, "MoreOptionsLayer::addToggle");
}
