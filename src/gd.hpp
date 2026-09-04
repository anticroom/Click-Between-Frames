#pragma once

// minimal 2.113 bindings, offsets from the windows binary

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <cstdint>
#include <string>
#include <cocos2d.h>

using namespace cocos2d;

inline uintptr_t gdBase() {
	static uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
	return base;
}

inline uintptr_t cocosBase() {
	static uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA("libcocos2d.dll"));
	return base;
}

enum class PlayerButton : int {
	Jump = 1,
	Left = 2,
	Right = 3
};

// our own ids for controller buttons, they just have to be unique
enum ControllerKeyCodes : int {
	CONTROLLER_A = 1000,
	CONTROLLER_B = 1001,
	CONTROLLER_X = 1002,
	CONTROLLER_Y = 1003,
	CONTROLLER_Start = 1004,
	CONTROLLER_Back = 1005,
	CONTROLLER_RB = 1006,
	CONTROLLER_LB = 1007,
	CONTROLLER_RT = 1008,
	CONTROLLER_LT = 1009,
	CONTROLLER_Up = 1010,
	CONTROLLER_Down = 1011,
	CONTROLLER_Left = 1012,
	CONTROLLER_Right = 1013,
	CONTROLLER_LTHUMBSTICK_UP = 1014,
	CONTROLLER_LTHUMBSTICK_DOWN = 1015,
	CONTROLLER_LTHUMBSTICK_LEFT = 1016,
	CONTROLLER_LTHUMBSTICK_RIGHT = 1017,
	CONTROLLER_RTHUMBSTICK_UP = 1018,
	CONTROLLER_RTHUMBSTICK_DOWN = 1019,
	CONTROLLER_RTHUMBSTICK_LEFT = 1020,
	CONTROLLER_RTHUMBSTICK_RIGHT = 1021
};

class PlayerObject {
public:
	char m_pad000[0x488];
	CCDictionary* m_collisionLogBottom;   // 0x488
	CCDictionary* m_collisionLogTop;      // 0x48C
	char m_pad490[0x4B8 - 0x490];
	int m_lastCollisionBottom;            // 0x4B8
	int m_lastCollisionTop;               // 0x4BC
	char m_pad4C0[0x628 - 0x4C0];
	double m_yVelocity;                   // 0x628
	bool m_isOnSlope;                     // 0x630
	bool m_wasOnSlope;                    // 0x631
	char m_pad632[0x638 - 0x632];
	bool m_isShip;                        // 0x638
	bool m_isBird;                        // 0x639
	bool m_isBall;                        // 0x63A
	bool m_isDart;                        // 0x63B
	bool m_isRobot;                       // 0x63C
	bool m_isSpider;                      // 0x63D
	bool m_isUpsideDown;                  // 0x63E
	char m_pad63F[1];
	bool m_isOnGround;                    // 0x640
	bool m_isDashing;                     // 0x641
	char m_pad642[0x66C - 0x642];
	CCArray* m_touchingRings;             // 0x66C
	char m_pad670[0x67C - 0x670];
	CCPoint m_position;                   // 0x67C
};

class PlayLayer {
public:
	char m_pad000[0x224];
	PlayerObject* m_player1;              // 0x224
	PlayerObject* m_player2;              // 0x228
	char m_pad22C[0x2A9 - 0x22C];
	bool m_isDualMode;                    // 0x2A9
	char m_pad2AA[0x39C - 0x2AA];
	bool m_playerDied;                    // 0x39C
	char m_pad39D[0x494 - 0x39D];
	bool m_isTestMode;                    // 0x494
	char m_pad495[0x4BD - 0x495];
	bool m_hasLevelCompleteMenu;          // 0x4BD
	char m_pad4BE[0x52F - 0x4BE];
	bool m_isPaused;                      // 0x52F

	static PlayLayer* get();

	void checkCollisions(PlayerObject* player, float dt);

	// the int argument is unused
	void pushButton(int button, bool isPlayer1) {
		reinterpret_cast<void(__thiscall*)(PlayLayer*, int, bool)>(gdBase() + 0x111500)(this, button, isPlayer1);
	}
	void releaseButton(int button, bool isPlayer1) {
		reinterpret_cast<void(__thiscall*)(PlayLayer*, int, bool)>(gdBase() + 0x111660)(this, button, isPlayer1);
	}
};

class GameManager {
public:
	char m_pad000[0x164];
	PlayLayer* m_playLayer;               // 0x164
	void* m_editorLayer;                  // 0x168

	static GameManager* sharedState() {
		return reinterpret_cast<GameManager*(__cdecl*)()>(gdBase() + 0xC4A50)();
	}

	PlayLayer* getPlayLayer() { return m_playLayer; }
	void* getEditorLayer() { return m_editorLayer; }
};

#pragma runtime_checks("s", off)
class FLAlertLayer {
public:
	// the caption is passed by value as an std::string so the caller has to clean the stack
	static FLAlertLayer* create(void* target, const char* title, const char* btn1, const char* btn2, std::string caption) {
		auto ret = reinterpret_cast<FLAlertLayer*(__fastcall*)(void*, const char*, const char*, const char*, std::string)>(
			gdBase() + 0x22680)(target, title, btn1, btn2, caption);
		__asm add esp, 0x20
		return ret;
	}
	void show() {
		reinterpret_cast<void(__thiscall*)(FLAlertLayer*)>(gdBase() + 0x23560)(this);
	}
};
#pragma runtime_checks("s", restore)

inline PlayLayer* PlayLayer::get() {
	return GameManager::sharedState()->getPlayLayer();
}
