#include "includes.hpp"
#include "log.hpp"
#include "options.hpp"

#include <limits>
#include <cmath>
#include <algorithm>
#include <string>
#include <unordered_map>

#include <MinHook.h>

constexpr double SMALLEST_FLOAT = std::numeric_limits<float>::min();

constexpr InputEvent EMPTY_INPUT = InputEvent {
	.time = 0,
	.inputType = PlayerButton::Jump,
	.inputState = false,
	.isPlayer1 = false,
};
constexpr Step EMPTY_STEP = Step {
	.input = EMPTY_INPUT,
	.deltaFactor = 1.0,
	.endStep = true,
};

std::deque<struct InputEvent> inputQueue;
std::deque<struct InputEvent> inputQueueCopy;
std::deque<struct Step> stepQueue;

std::atomic<bool> softToggle;

InputEvent nextInput = EMPTY_INPUT;

TimestampType lastFrameTime;
TimestampType currentFrameTime;

bool firstFrame = true; // necessary to prevent accidental inputs at the start of the level or when unpausing
bool skipUpdate = true; // true -> dont split steps during PlayerObject::update()
bool enableInput = false;
bool linuxNative = false;
bool lateCutoff; // false -> ignore inputs that happen after the start of the frame; true -> check for inputs at the latest possible moment

std::array<std::unordered_set<size_t>, 6> inputBinds;
std::unordered_set<uint16_t> heldInputs;

std::mutex inputQueueLock;
std::mutex keybindsLock;

std::atomic<bool> enableRightClick;
bool threadPriority;

void (__thiscall* PlayLayer_update)(PlayLayer*, float);
void (__thiscall* PlayerObject_update)(PlayerObject*, float);

/*
this function copies over the inputQueue from the input thread and uses it to build a queue of physics steps
based on when each input happened relative to the start of the frame
(and also calculates the associated stepDelta multipliers for each step)
*/
void buildStepQueue(int stepCount) {
	PlayLayer* playLayer = PlayLayer::get();
	nextInput = EMPTY_INPUT;
	stepQueue = {}; // shouldnt be necessary, but just in case

	if (lateCutoff) { // copy all inputs in queue, use current time as the frame boundary
		currentFrameTime = getCurrentTimestamp();
		if (linuxNative) {
			linuxCheckInputs();
		}

		std::lock_guard lock(inputQueueLock);
		inputQueueCopy = inputQueue;
		inputQueue = {};
	}
	else { // only copy inputs that happened before the start of the frame
		if (linuxNative) linuxCheckInputs();

		std::lock_guard lock(inputQueueLock);
		while (!inputQueue.empty() && inputQueue.front().time <= currentFrameTime) {
			inputQueueCopy.push_back(inputQueue.front());
			inputQueue.pop_front();
		}
	}

	skipUpdate = false;
	if (firstFrame) {
		skipUpdate = true;
		firstFrame = false;
		lastFrameTime = currentFrameTime;
		if (!lateCutoff) inputQueueCopy = {};
		return;
	}

	TimestampType deltaTime = currentFrameTime - lastFrameTime;
	TimestampType stepDelta = (deltaTime / stepCount) + 1; // the +1 is to prevent dropped inputs caused by integer division

	for (int i = 0; i < stepCount; i++) { // for each physics step of the frame
		double elapsedTime = 0.0;
		while (!inputQueueCopy.empty()) { // while loop to account for multiple inputs on the same step
			InputEvent front = inputQueueCopy.front();

			if (front.time - lastFrameTime < stepDelta * (i + 1)) { // if the first input in the queue happened on the current step
				double inputTime = static_cast<double>((front.time - lastFrameTime) % stepDelta) / stepDelta; // proportion of step elapsed at the time the input was made
				stepQueue.emplace_back(Step{ front, std::clamp(inputTime - elapsedTime, SMALLEST_FLOAT, 1.0), false });
				inputQueueCopy.pop_front();
				elapsedTime = inputTime;
			}
			else break; // no more inputs this step, more later in the frame
		}

		stepQueue.emplace_back(Step{ EMPTY_INPUT, std::max(SMALLEST_FLOAT, 1.0 - elapsedTime), true });
	}

	lastFrameTime = currentFrameTime;
}

/*
return the first step in the queue,
also check if an input happened on the previous step, if so run pushButton/releaseButton
(2.1's split version of handleButton).
tbh this doesnt need to be a separate function from the PlayerObject::update() hook
*/
Step popStepQueue() {
	if (stepQueue.empty()) return EMPTY_STEP;

	Step front = stepQueue.front();
	double deltaFactor = front.deltaFactor;

	if (nextInput.time != 0) {
		PlayLayer* playLayer = PlayLayer::get();

		enableInput = true;
		if (nextInput.inputState == Press) playLayer->pushButton(0, nextInput.isPlayer1);
		else playLayer->releaseButton(0, nextInput.isPlayer1);
		enableInput = false;
	}

	nextInput = front.input;
	stepQueue.pop_front();

	return front;
}
static size_t keyFromName(const std::string& name) {
	static const std::pair<const char*, size_t> NAMES[] = {
		{ "space", KEY_Space }, { "up", KEY_Up }, { "down", KEY_Down },
		{ "left", KEY_Left }, { "right", KEY_Right }, { "enter", KEY_Enter },
		{ "shift", KEY_Shift }, { "control", KEY_Control }, { "ctrl", KEY_Control },
		{ "alt", KEY_Alt }, { "tab", KEY_Tab }
	};

	std::string key = name;
	key.erase(0, key.find_first_not_of(" 	"));
	key.erase(key.find_last_not_of(" 	") + 1);
	std::transform(key.begin(), key.end(), key.begin(), ::tolower);
	if (key.empty()) return 0;

	for (auto& [text, code] : NAMES) if (key == text) return code;
	if (key.size() == 1) { // a key is its own vkey
		char c = static_cast<char>(::toupper(key[0]));
		if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return c;
	}
	if (key.size() > 2 && key[0] == '0' && key[1] == 'x') return strtoul(key.c_str(), nullptr, 16);
	if (isdigit(key[0])) return strtoul(key.c_str(), nullptr, 10);

	cbf::log::error("Unknown key name \"%s\"", name.c_str());
	return 0;
}

static void parseKeyList(std::unordered_set<size_t>& binds, const std::string& list) {
	size_t start = 0;
	while (start <= list.size()) {
		size_t end = list.find(',', start);
		if (end == std::string::npos) end = list.size();
		size_t code = keyFromName(list.substr(start, end - start));
		if (code) binds.emplace(code);
		start = end + 1;
	}
}

/*
send list of keybinds to the input thread.
2.1 has no rebindable controls of its own so the jump keys come from the ini
*/
void updateKeybinds() {
	std::array<std::unordered_set<size_t>, 6> binds;

	enableRightClick.store(settings::getBool("right-click", false));

	binds[p1Jump] = { CONTROLLER_A, CONTROLLER_RB };
	binds[p2Jump] = { CONTROLLER_LB };
	parseKeyList(binds[p1Jump], settings::getString("p1-jump-keys", "space"));
	parseKeyList(binds[p2Jump], settings::getString("p2-jump-keys", "up"));

	{
		std::lock_guard lock(keybindsLock);
		inputBinds = binds;
	}
}

bool passThroughInput = false;
int vanillaButtonCount = 0;
bool vanillaButtonPlayer1 = true;
std::unordered_map<int, bool> learnedKeys;

static bool isBoundKey(int key) {
	std::lock_guard lock(keybindsLock);
	for (auto& binds : inputBinds) if (binds.contains(static_cast<size_t>(key))) return true;
	return false;
}
static void learnKey(int key, bool down) {
	if (down) {
		if (vanillaButtonCount > 0) learnedKeys[key] = vanillaButtonPlayer1;
		return;
	}
	auto learned = learnedKeys.find(key);
	if (learned == learnedKeys.end()) return;
	{
		std::lock_guard lock(keybindsLock);
		inputBinds[learned->second ? p1Jump : p2Jump].emplace(static_cast<size_t>(key));
	}
	cbf::log::info("found jump key %d for player %d", key, learned->second ? 1 : 2);
	learnedKeys.erase(learned);
}
void decomp_resetCollisionLog(PlayerObject* p) {
	p->m_collisionLogTop->removeAllObjects();
	p->m_collisionLogBottom->removeAllObjects();
	p->m_lastCollisionBottom = 0;
	p->m_lastCollisionTop = 0;
}

bool safeMode;
bool mouseFix;
bool clickOnSteps = false;

int stepCount;
double frameDelta;
bool stepsBuilt = false;

// work the step count back out of the per step delta, so TPS/FPS bypasses still work
void calculateSteps(float stepDelta) {
	PlayLayer* pl = PlayLayer::get();
	stepsBuilt = true;

	stepCount = static_cast<int>(std::round((frameDelta * 60.0) / stepDelta));
	if (stepCount < 1) stepCount = 1;

	if (pl->m_playerDied || GameManager::sharedState()->getEditorLayer() || softToggle.load()) {
		enableInput = true;
		skipUpdate = true;
		firstFrame = true;
	}
	else if (frameDelta > 0.0) buildStepQueue(stepCount);
	else skipUpdate = true;
}

void onFrameStart() {
	PlayLayer* playLayer = PlayLayer::get();

	if (!lateCutoff) {
		currentFrameTime = getCurrentTimestamp();
	}

	if (softToggle.load() // CBF disabled
		|| !GetFocus() // GD is minimized
		|| !playLayer // not in level
		|| playLayer->m_isPaused // if paused
		|| playLayer->m_hasLevelCompleteMenu) // if on endscreen
	{
		firstFrame = true;
		skipUpdate = true;
		enableInput = true;

		inputQueueCopy = {};

		if (!linuxNative) { // clearing the queue isnt necessary on Linux since its fixed size anyway, but on Windows memory leaks are possible
			std::lock_guard lock(inputQueueLock);
			inputQueue = {};
		}
	}
	if (mouseFix && !skipUpdate) { // reduce lag with high polling rate mice by limiting the number of mouse movements per frame to 1
		MSG msg;
		int index = 1;
		while (PeekMessage(&msg, NULL, WM_MOUSEFIRST + index, WM_MOUSELAST, PM_NOREMOVE)) { // check for mouse inputs in the queue
			if (msg.message == WM_MOUSEMOVE || msg.message == WM_NCMOUSEMOVE) {
				PeekMessage(&msg, NULL, WM_MOUSEFIRST + index, WM_MOUSELAST, PM_REMOVE); // remove mouse movements from queue
			}
			else index++;
		}
	}
}
bool (__thiscall* CCKeyboardDispatcher_dispatchKeyboardMSG)(void*, int, bool);
bool __fastcall CCKeyboardDispatcher_dispatchKeyboardMSG_H(void* self, void*, int key, bool down) {
	if (isBoundKey(key)) return CCKeyboardDispatcher_dispatchKeyboardMSG(self, key, down);
	passThroughInput = true;
	vanillaButtonCount = 0;
	bool result = CCKeyboardDispatcher_dispatchKeyboardMSG(self, key, down);
	passThroughInput = false;
	learnKey(key, down);

	return result;
}

void (__thiscall* CCEGLView_pollEvents)(void*);
void __fastcall CCEGLView_pollEvents_H(void* self, void*) {
	onFrameStart();

	CCEGLView_pollEvents(self);
}

// no getModifiedDelta in 2.1, so just record the delta and notice if the step loop didnt run
void __fastcall PlayLayer_update_H(PlayLayer* self, void*, float dt) {
	frameDelta = dt;
	stepsBuilt = false;

	PlayLayer_update(self, dt);

	if (!stepsBuilt) { // no physics steps happened this frame (dead, paused, level not started yet, ...)
		enableInput = true;
		skipUpdate = true;
		firstFrame = true;
	}
}

// disable regular inputs while CBF is active
void (__thiscall* GJBaseGameLayer_pushButton)(PlayLayer*, int, bool);
void __fastcall GJBaseGameLayer_pushButton_H(PlayLayer* self, void*, int button, bool isPlayer1) {
	if (passThroughInput) {
		vanillaButtonCount++;
		vanillaButtonPlayer1 = isPlayer1;
	}
	if (enableInput || passThroughInput) GJBaseGameLayer_pushButton(self, button, isPlayer1);
}

void (__thiscall* GJBaseGameLayer_releaseButton)(PlayLayer*, int, bool);
void __fastcall GJBaseGameLayer_releaseButton_H(PlayLayer* self, void*, int button, bool isPlayer1) {
	if (enableInput || passThroughInput) GJBaseGameLayer_releaseButton(self, button, isPlayer1);
}

bool inputThisStep = false;
bool p1Split = false;
bool p2Split = false;
bool midStep = false;

// split a single step based on the entries in stepQueue
void __fastcall PlayerObject_update_H(PlayerObject* self, void*, float stepDelta) {
	PlayLayer* pl = PlayLayer::get();
	if (!skipUpdate) enableInput = false;

	if (pl && self != pl->m_player1 || midStep) { // do all of the logic during the P1 update for simplicity
		if (midStep || !inputThisStep || self != pl->m_player2) PlayerObject_update(self, stepDelta);
		return;
	}

	// this is the first player update of the frame, so it's the earliest point the step count is known
	if (pl && !stepsBuilt && stepDelta > 0.0f) calculateSteps(stepDelta);

	if (clickOnSteps && !stepQueue.empty()) {
		Step step;
		do step = popStepQueue(); while (!stepQueue.empty() && !step.endStep); // process 1 step (or more if theres an input)
	}

	inputThisStep = stepQueue.empty() ? false : !stepQueue.front().endStep;
	if (!stepQueue.empty() && !inputThisStep && !clickOnSteps) stepQueue.pop_front();

	if (skipUpdate
		|| !pl
		|| !inputThisStep
		|| clickOnSteps)
	{
		p1Split = false;
		p2Split = false;
		inputThisStep = false;
		PlayerObject_update(self, stepDelta);
		return;
	}

	PlayerObject* p2 = pl->m_player2;
	bool isDual = pl->m_isDualMode;
	bool p1StartedOnGround = self->m_isOnGround;
	bool p2StartedOnGround = p2->m_isOnGround;

	bool p1NotBuffering = p1StartedOnGround
		|| self->m_touchingRings->count()
		|| self->m_isDashing
		|| (self->m_isDart || self->m_isBird || self->m_isShip);

	bool p2NotBuffering = p2StartedOnGround
		|| p2->m_touchingRings->count()
		|| p2->m_isDashing
		|| (p2->m_isDart || p2->m_isBird || p2->m_isShip);

	p1Split = p1NotBuffering;
	p2Split = p2NotBuffering && isDual;

	Step step;
	bool firstLoop = true;
	midStep = true;

	do {
		step = popStepQueue();
		const float substepDelta = stepDelta * step.deltaFactor;

		if (p1Split) {
			PlayerObject_update(self, substepDelta);
			if (!step.endStep) {
				if (firstLoop && ((self->m_yVelocity < 0) ^ self->m_isUpsideDown)) self->m_isOnGround = p1StartedOnGround; // this fixes delayed inputs on platforms moving down for some reason
				if (!self->m_isOnSlope || self->m_isDart) pl->checkCollisions(self, 0.0f); // moving platforms will launch u really high if this is anything other than 0.0, idk why
				else pl->checkCollisions(self, stepDelta); // slopes will launch you really high if the 2nd argument is lower than like 0.01, idk why
				decomp_resetCollisionLog(self); // necessary for wave
			}
		}
		else if (step.endStep) PlayerObject_update(self, stepDelta); // revert to click-on-steps mode when buffering to reduce bugs

		if (p2Split) {
			PlayerObject_update(p2, substepDelta);
			if (!step.endStep) {
				if (firstLoop && ((p2->m_yVelocity < 0) ^ p2->m_isUpsideDown)) p2->m_isOnGround = p2StartedOnGround;
				if (!p2->m_isOnSlope || p2->m_isDart) pl->checkCollisions(p2, 0.0f);
				else pl->checkCollisions(p2, stepDelta);
				decomp_resetCollisionLog(p2);
			}
		}
		else if (step.endStep && isDual) PlayerObject_update(p2, stepDelta); // 2.1 only updates P2 in dual mode

		firstLoop = false;
	} while (!step.endStep);

	midStep = false;
}

// update keybinds when you enter a level
bool (__thiscall* PlayLayer_init)(PlayLayer*, void*);
bool __fastcall PlayLayer_init_H(PlayLayer* self, void*, void* level) {
	updateKeybinds();
	return PlayLayer_init(self, level);
}

// disable progress in safe mode
void (__thiscall* PlayLayer_levelComplete)(PlayLayer*);
void __fastcall PlayLayer_levelComplete_H(PlayLayer* self, void*) {
	bool testMode = self->m_isTestMode;
	if (safeMode && !softToggle.load()) self->m_isTestMode = true;

	PlayLayer_levelComplete(self);

	self->m_isTestMode = testMode;
}

// disable new best popup in safe mode
void (__thiscall* PlayLayer_showNewBest)(PlayLayer*, bool, int, int, bool, bool, bool);
void __fastcall PlayLayer_showNewBest_H(PlayLayer* self, void*, bool p0, int p1, int p2, bool p3, bool p4, bool p5) {
	if (!safeMode || softToggle.load()) PlayLayer_showNewBest(self, p0, p1, p2, p3, p4, p5);
}

// CBF endscreen watermark
void (__thiscall* EndLevelLayer_customSetup)(void*);
void __fastcall EndLevelLayer_customSetup_H(void* self, void*) {
	EndLevelLayer_customSetup(self);

	if (!softToggle.load() && !clickOnSteps) {
		cocos2d::CCSize size = cocos2d::CCDirector::sharedDirector()->getWinSize();
		CCLabelBMFont* indicator = CCLabelBMFont::create("CBF", "bigFont.fnt");

		indicator->setPosition({ size.width, size.height });
		indicator->setAnchorPoint({ 1.0f, 1.0f });
		indicator->setOpacity(30);
		indicator->setScale(0.2f);

		reinterpret_cast<CCNode*>(self)->addChild(indicator);
	}
}
// the saved settings live in GD's game variables, which are only readable once the game is up
bool (__thiscall* MenuLayer_init)(void*);
bool __fastcall MenuLayer_init_H(void* self, void*) {
	if (!MenuLayer_init(self)) return false;	
	syncSettingsFromGame();
	return true;
}

// notify the player if theres an issue with input on Linux
bool (__thiscall* CreatorLayer_init)(void*);
bool __fastcall CreatorLayer_init_H(void* self, void*) {
	if (!CreatorLayer_init(self)) return false;

	showLinuxInputError();
	return true;
}

void toggleMod(bool disable) {
	// 2.1 has no vanilla click-between-steps/click-on-steps to patch out, so there is nothing to disable here
	softToggle.store(disable);
}

// push a value changed in the options page into the running mod
void applySetting(const char* key, bool value) {
	std::string setting = key;

	if (setting == "soft-toggle") toggleMod(value);
	else if (setting == "safe-mode") safeMode = value;
	else if (setting == "click-on-steps") clickOnSteps = value;
	else if (setting == "mouse-fix") mouseFix = value;
	else if (setting == "late-cutoff") lateCutoff = value;
	else if (setting == "right-click") { enableRightClick.store(value); updateKeybinds(); }
	else if (setting == "thread-priority") setThreadPriority(value);
	// thread-priority and wine-workaround are only read while the mod is loading
}

static void hook(uintptr_t address, void* detour, void* trampoline) {
	MH_STATUS status = MH_CreateHook(reinterpret_cast<void*>(address), detour, reinterpret_cast<void**>(trampoline));
	if (status != MH_OK) cbf::log::error("Failed to hook %p: %s", reinterpret_cast<void*>(address), MH_StatusToString(status));
}

void modLoaded() {
	settings::load();

	toggleMod(settings::getBool("soft-toggle", false));

	safeMode = settings::getBool("safe-mode", false);
	clickOnSteps = settings::getBool("click-on-steps", false);
	mouseFix = settings::getBool("mouse-fix", false);
	lateCutoff = settings::getBool("late-cutoff", false);
	threadPriority = settings::getBool("thread-priority", false);

	updateKeybinds();

	MH_Initialize();

	const uintptr_t base = gdBase();

	hook(base + 0x2029C0, &PlayLayer_update_H, &PlayLayer_update);
	hook(base + 0x1E8200, &PlayerObject_update_H, &PlayerObject_update);
	hook(base + 0x111500, &GJBaseGameLayer_pushButton_H, &GJBaseGameLayer_pushButton);
	hook(base + 0x111660, &GJBaseGameLayer_releaseButton_H, &GJBaseGameLayer_releaseButton);
	hook(base + 0x1FB780, &PlayLayer_init_H, &PlayLayer_init);
	hook(base + 0x1FD3D0, &PlayLayer_levelComplete_H, &PlayLayer_levelComplete);
	hook(base + 0x1FE3A0, &PlayLayer_showNewBest_H, &PlayLayer_showNewBest);
	hook(base + 0x94CB0, &EndLevelLayer_customSetup_H, &EndLevelLayer_customSetup);
	hook(base + 0x4DE40, &CreatorLayer_init_H, &CreatorLayer_init);
	hook(base + 0x1907B0, &MenuLayer_init_H, &MenuLayer_init);

	if (HMODULE cocos = GetModuleHandleA("libcocos2d.dll")) {
		if (void* pollEvents = reinterpret_cast<void*>(GetProcAddress(cocos, "?pollEvents@CCEGLView@cocos2d@@QAEXXZ"))) {
			hook(reinterpret_cast<uintptr_t>(pollEvents), &CCEGLView_pollEvents_H, &CCEGLView_pollEvents);
		}
		else cbf::log::error("Failed to find CCEGLView::pollEvents");
		if (void* dispatch = reinterpret_cast<void*>(GetProcAddress(cocos, "?dispatchKeyboardMSG@CCKeyboardDispatcher@cocos2d@@QAE_NW4enumKeyCodes@2@_N@Z"))) {
			hook(reinterpret_cast<uintptr_t>(dispatch), &CCKeyboardDispatcher_dispatchKeyboardMSG_H, &CCKeyboardDispatcher_dispatchKeyboardMSG);
		}
		else cbf::log::error("Failed to find CCKeyboardDispatcher::dispatchKeyboardMSG");
	}

	setupOptionsHook();

	MH_EnableHook(MH_ALL_HOOKS);

	windowsSetup();
}
