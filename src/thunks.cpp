#include "gd.hpp"

// checkCollisions takes the player on the stack and the delta in xmm2, so it needs asm.
// the callee cleans up the pushed argument itself
#pragma runtime_checks("s", off)
void PlayLayer::checkCollisions(PlayerObject* player, float dt) {
	static uintptr_t address = gdBase() + 0x203CD0;
	PlayLayer* self = this;

	__asm {
		movss xmm2, dt
		push player
		mov ecx, self
		call address
	}
}
#pragma runtime_checks("s", restore)
