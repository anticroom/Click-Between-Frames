#include "includes.hpp"
#include "log.hpp"

#include <Xinput.h>

extern HMODULE g_module;

TimestampType getCurrentTimestamp() {
	LARGE_INTEGER t;
	if (linuxNative) {
		// used instead of QPC to make it possible to convert between Linux and Windows timestamps
		GetSystemTimePreciseAsFileTime((FILETIME*)&t);
	} else {
		QueryPerformanceCounter(&t);
	}
	return t.QuadPart;
}

LPVOID pBuf;
HANDLE hSharedMem = NULL;
HANDLE hMutex = NULL;
static HANDLE inputThreads[2] = { NULL, NULL };
void setThreadPriority(bool high) {
	threadPriority = high;
	for (HANDLE thread : inputThreads) {
		if (thread) SetThreadPriority(thread, high ? THREAD_PRIORITY_HIGHEST : THREAD_PRIORITY_NORMAL);
	}
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	LARGE_INTEGER time;
	PlayerButton inputType;
	bool inputState;
	bool player1;

	LPVOID pData;
	switch (uMsg) {
	case WM_INPUT: {

		UINT dwSize;
		GetRawInputData((HRAWINPUT)lParam, RID_INPUT, NULL, &dwSize, sizeof(RAWINPUTHEADER));

		auto lpb = std::unique_ptr<BYTE[]>(new BYTE[dwSize]);
		if (!lpb) {
			return 0;
		}
		if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, lpb.get(), &dwSize, sizeof(RAWINPUTHEADER)) != dwSize) {
			cbf::log::debug("GetRawInputData does not return correct size");
		}

		RAWINPUT* raw = (RAWINPUT*)lpb.get();
		switch (raw->header.dwType) {
		case RIM_TYPEKEYBOARD: {
			QueryPerformanceCounter(&time);

			USHORT vkey = raw->data.keyboard.VKey;
			inputState = raw->data.keyboard.Flags & RI_KEY_BREAK ? Release : Press;

			if (vkey >= VK_NUMPAD0 && vkey <= VK_NUMPAD9) vkey -= 0x30; // make numpad numbers work with customkeybinds

			// cocos2d::enumKeyCodes corresponds directly to vkeys
			if (heldInputs.contains(vkey)) {
				if (inputState) return 0;
				else heldInputs.erase(vkey);
			}

			bool shouldEmplace = true;
			player1 = true;

			{
				std::lock_guard lock(keybindsLock);

				if (inputBinds[p1Jump].contains(vkey)) inputType = PlayerButton::Jump;
				else if (inputBinds[p1Left].contains(vkey)) inputType = PlayerButton::Left;
				else if (inputBinds[p1Right].contains(vkey)) inputType = PlayerButton::Right;
				else {
					player1 = false;
					if (inputBinds[p2Jump].contains(vkey)) inputType = PlayerButton::Jump;
					else if (inputBinds[p2Left].contains(vkey)) inputType = PlayerButton::Left;
					else if (inputBinds[p2Right].contains(vkey)) inputType = PlayerButton::Right;
					else shouldEmplace = false;
				}
			}

			if (inputState) heldInputs.emplace(vkey);
			if (!shouldEmplace) return 0;

			break;
		}
		case RIM_TYPEMOUSE: {
			USHORT flags = raw->data.mouse.usButtonFlags;
			bool shouldEmplace = true;

			player1 = true;
			inputType = PlayerButton::Jump;

			if (flags & RI_MOUSE_BUTTON_1_DOWN) inputState = Press;
			else if (flags & RI_MOUSE_BUTTON_1_UP) inputState = Release;
			else {
				player1 = false;
				if (!enableRightClick.load()) return 0;
				if (flags & RI_MOUSE_BUTTON_2_DOWN) inputState = Press;
				else if (flags & RI_MOUSE_BUTTON_2_UP) inputState = Release;
				else return 0;
			}

			QueryPerformanceCounter(&time); // dont call on mouse move events
			break;
		}
		default:
			return 0;
		}
		break;
	}
	default:
		return DefWindowProcA(hwnd, uMsg, wParam, lParam);
	}

	{
		std::lock_guard lock(inputQueueLock);
		inputQueue.emplace_back(InputEvent{ timestampFromLarge(time), inputType, inputState, player1 });
	}

	return 0;
}

void rawInputThread() {
	WNDCLASS wc = {};
	wc.lpfnWndProc = WindowProc;
	wc.hInstance = GetModuleHandleA(NULL);
	wc.lpszClassName = "CBF";

	RegisterClass(&wc);
	HWND hwnd = CreateWindow("CBF", "Raw Input Window", 0, 0, 0, 0, 0, HWND_MESSAGE, 0, wc.hInstance, 0);
	if (!hwnd) {
		const DWORD err = GetLastError();
		cbf::log::error("Failed to create raw input window: %lu", err);
		return;
	}

	RAWINPUTDEVICE dev[2];
	dev[0].usUsagePage = 0x01;        // generic desktop controls
	dev[0].usUsage = 0x02;            // mouse
	dev[0].dwFlags = RIDEV_INPUTSINK; // allow inputs without being in the foreground
	dev[0].hwndTarget = hwnd;         // raw input window

	dev[1].usUsagePage = 0x01;
	dev[1].usUsage = 0x06;            // keyboard
	dev[1].dwFlags = RIDEV_INPUTSINK;
	dev[1].hwndTarget = hwnd;

	if (!RegisterRawInputDevices(dev, 2, sizeof(dev[0]))) {
		cbf::log::error("Failed to register raw input devices");
		return;
	}

	cbf::log::info("raw input registered"); // debug logging

	if (threadPriority) SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

	MSG msg;
    while (GetMessage(&msg, hwnd, 0, 0)) {
        DispatchMessage(&msg);
        while (softToggle.load()) { // reduce lag while mod is disabled
            Sleep(2000);
            while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)); // clear all pending messages
        }
    }
}

void xinputThread() {
    HMODULE xinputLib = LoadLibrary("Xinput1_4.dll");
    if (xinputLib == NULL) xinputLib = LoadLibrary("XInput9_1_0.dll"); // the version 2.1 itself links against
    if (xinputLib == NULL) {
        cbf::log::error("Failed to load Xinput");
        return;
    }
    typedef DWORD(WINAPI* XInputGetState_t)(DWORD, XINPUT_STATE*);
    const auto XInputGetState = (XInputGetState_t)GetProcAddress(xinputLib, "XInputGetState");

    // cocos2d doesn't match with Xinput
    std::array<std::pair<int, int>, 22> xinputToCCKey = {
        std::pair { XINPUT_GAMEPAD_DPAD_UP, CONTROLLER_Up },
        std::pair { XINPUT_GAMEPAD_DPAD_DOWN, CONTROLLER_Down },
        std::pair { XINPUT_GAMEPAD_DPAD_LEFT, CONTROLLER_Left },
        std::pair { XINPUT_GAMEPAD_DPAD_RIGHT, CONTROLLER_Right },
        std::pair { XINPUT_GAMEPAD_START, CONTROLLER_Start },
        std::pair { XINPUT_GAMEPAD_BACK, CONTROLLER_Back },
        std::pair { XINPUT_GAMEPAD_LEFT_SHOULDER, CONTROLLER_LB },
        std::pair { XINPUT_GAMEPAD_RIGHT_SHOULDER, CONTROLLER_RB },
        std::pair { XINPUT_GAMEPAD_A, CONTROLLER_A },
        std::pair { XINPUT_GAMEPAD_B, CONTROLLER_B },
        std::pair { XINPUT_GAMEPAD_X, CONTROLLER_X },
        std::pair { XINPUT_GAMEPAD_Y, CONTROLLER_Y },
        std::pair { -1, CONTROLLER_LT },
        std::pair { -1, CONTROLLER_RT },
        std::pair { -1, CONTROLLER_LTHUMBSTICK_UP },
        std::pair { -1, CONTROLLER_LTHUMBSTICK_DOWN },
        std::pair { -1, CONTROLLER_LTHUMBSTICK_LEFT },
        std::pair { -1, CONTROLLER_LTHUMBSTICK_RIGHT },
        std::pair { -1, CONTROLLER_RTHUMBSTICK_UP },
        std::pair { -1, CONTROLLER_RTHUMBSTICK_DOWN },
        std::pair { -1, CONTROLLER_RTHUMBSTICK_LEFT },
        std::pair { -1, CONTROLLER_RTHUMBSTICK_RIGHT }
    };

    if (threadPriority) SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    bool xinputWorks = false;

    do {
        std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

        DWORD dwResult;
        for (DWORD i = 0; i < XUSER_MAX_COUNT; i++ ) {
            XINPUT_STATE state;
            dwResult = XInputGetState(i, &state);

            // controller not connected
            if (dwResult != ERROR_SUCCESS) {
                continue;
            }

            xinputWorks = true;

            for (auto& [xinputButton, ccButton] : xinputToCCKey) {
                bool inputState;

                bool buttonPressed = heldInputs.contains(ccButton);
                // if it's not a joystick or trigger, we can just use & to check if the button is pressed
                if (xinputButton != -1) {
                    buttonPressed = state.Gamepad.wButtons & xinputButton;
                } else { // otherwise we have to check the values
                    switch (ccButton) {
                    case CONTROLLER_LT:
                        buttonPressed = state.Gamepad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
                        break;
                    case CONTROLLER_RT:
                        buttonPressed = state.Gamepad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
                        break;
                    case CONTROLLER_LTHUMBSTICK_UP:
                        buttonPressed = state.Gamepad.sThumbLY > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
                        break;
                    case CONTROLLER_LTHUMBSTICK_DOWN:
                        buttonPressed = state.Gamepad.sThumbLY < -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
                        break;
                    case CONTROLLER_LTHUMBSTICK_LEFT:
                        buttonPressed = state.Gamepad.sThumbLX < -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
                        break;
                    case CONTROLLER_LTHUMBSTICK_RIGHT:
                        buttonPressed = state.Gamepad.sThumbLX > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
                        break;
                    case CONTROLLER_RTHUMBSTICK_UP:
                        buttonPressed = state.Gamepad.sThumbRY > XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE;
                        break;
                    case CONTROLLER_RTHUMBSTICK_DOWN:
                        buttonPressed = state.Gamepad.sThumbRY < -XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE;
                        break;
                    case CONTROLLER_RTHUMBSTICK_LEFT:
                        buttonPressed = state.Gamepad.sThumbRX < -XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE;
                        break;
                    case CONTROLLER_RTHUMBSTICK_RIGHT:
                        buttonPressed = state.Gamepad.sThumbRX > XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE;
                        break;
                    default:
                        // shouldn't be possible, just to prevent warnings
                        continue;
                    }
                }

                if (buttonPressed) {
                    if (heldInputs.contains(ccButton)) continue; // skip if already held
                    heldInputs.emplace(ccButton);
                    inputState = Press;
                } else {
                    if (!heldInputs.contains(ccButton)) continue; // skip if not held
                    heldInputs.erase(ccButton);
                    inputState = Release;
                }

                LARGE_INTEGER time;
                QueryPerformanceCounter(&time);
                PlayerButton inputType;
                bool player1 = true;

                {
                    std::lock_guard lock(keybindsLock);

                    if (inputBinds[p1Jump].contains(ccButton)) inputType = PlayerButton::Jump;
                    else if (inputBinds[p1Left].contains(ccButton)) inputType = PlayerButton::Left;
                    else if (inputBinds[p1Right].contains(ccButton)) inputType = PlayerButton::Right;
                    else {
                        player1 = false;
                        if (inputBinds[p2Jump].contains(ccButton)) inputType = PlayerButton::Jump;
                        else if (inputBinds[p2Left].contains(ccButton)) inputType = PlayerButton::Left;
                        else if (inputBinds[p2Right].contains(ccButton)) inputType = PlayerButton::Right;
                        else continue;
                    }
                }
                {
                    std::lock_guard lock(inputQueueLock);
                    inputQueue.emplace_back(InputEvent{ timestampFromLarge(time), inputType, inputState, player1 });
                }
            }
        }
        std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
        std::chrono::nanoseconds elapsed = end - start;
        // reduce lag, inputs should still be accurate to 1/2000th of a second
        std::this_thread::sleep_for(std::chrono::nanoseconds(500000) - elapsed);
        while (softToggle.load()) { // reduce lag while mod is disabled
            Sleep(2000);
        }
    } while (xinputWorks);

    if (!xinputWorks) {
        cbf::log::error("Xinput failed to read any controllers");
    }
}

// notify the player if theres an issue with input on Linux, called from the CreatorLayer::init hook
void showLinuxInputError() {
	if (!linuxNative) return;

	DWORD waitResult = WaitForSingleObject(hMutex, 5);
	if (waitResult == WAIT_OBJECT_0) {
		if (static_cast<LinuxInputEvent*>(pBuf)[0].type == 3 && !softToggle.load()) {
			cbf::log::error("Linux input failed");
			FLAlertLayer* popup = FLAlertLayer::create(
				nullptr,
				"CBF Linux",
				"OK",
				nullptr,
				"Failed to read input devices.\nOn most distributions, this can be resolved with the following command: <cr>sudo usermod -aG input $USER</c> (reboot afterward; this will make your system slightly less secure).\nIf the issue persists, please contact the mod developer."
			);
			popup->show();
		}
		ReleaseMutex(hMutex);
	}
	else if (waitResult == WAIT_TIMEOUT) {
		cbf::log::error("Mutex stalling");
	}
}

void linuxCheckInputs() {
	static std::unordered_map<int, int> linuxToCCKey = {
		{ BTN_A, CONTROLLER_A },
		{ BTN_B, CONTROLLER_B },
		{ BTN_X, CONTROLLER_X },
		{ BTN_Y, CONTROLLER_Y },
		{ BTN_TL, CONTROLLER_LB },
		{ BTN_TR, CONTROLLER_RB },
		{ BTN_SELECT, CONTROLLER_Back },
		{ BTN_START, CONTROLLER_Start },
	};

	DWORD waitResult = WaitForSingleObject(hMutex, 1);
	if (waitResult == WAIT_OBJECT_0) {
		LinuxInputEvent* events = static_cast<LinuxInputEvent*>(pBuf);
		for (int i = 0; i < BUFFER_SIZE; i++) {
			if (events[i].type == 0) break; // if there are no more events

			InputEvent input;
			bool player1 = true;
			USHORT scanCode = events[i].code;
			int value = events[i].value;

			switch (events[i].deviceType) {
			case MOUSE:
			case TOUCHPAD:
				if (scanCode == BUTTON_LEFT) {
					input.inputType = PlayerButton::Jump;
				}
				else if (scanCode == BUTTON_RIGHT) {
					if (!enableRightClick.load()) continue;
					input.inputType = PlayerButton::Jump;
					player1 = false;
				}
				break;
			case KEYBOARD: {
				USHORT keyCode = MapVirtualKeyExA(scanCode, MAPVK_VSC_TO_VK, GetKeyboardLayout(0));
				if (inputBinds[p1Jump].contains(keyCode)) input.inputType = PlayerButton::Jump;
				else if (inputBinds[p1Left].contains(keyCode)) input.inputType = PlayerButton::Left;
				else if (inputBinds[p1Right].contains(keyCode)) input.inputType = PlayerButton::Right;
				else {
					player1 = false;
					if (inputBinds[p2Jump].contains(keyCode)) input.inputType = PlayerButton::Jump;
					else if (inputBinds[p2Left].contains(keyCode)) input.inputType = PlayerButton::Left;
					else if (inputBinds[p2Right].contains(keyCode)) input.inputType = PlayerButton::Right;
					else continue;
				}
				break;
			}
			case TOUCHSCREEN:
				if (scanCode == BTN_TOUCH) { // touching screen
					input.inputType = PlayerButton::Jump;
				}
				break;
			case CONTROLLER: {
				int keyCode = -1;
				if (events[i].type == EV_KEY) {
					keyCode = linuxToCCKey[scanCode];
				} else if (events[i].type == EV_ABS) {
					bool continueLoop = false;
					auto analyze4Directions = [&] (int deadzone, int negative, int positive) {
						if (events[i].value < -deadzone) {
							keyCode = negative;
							if (heldInputs.contains(negative)) {
								continueLoop = true; // already held, ignore
							}
							value = Press;
						} else if (events[i].value > deadzone) {
							keyCode = positive;
							if (heldInputs.contains(positive)) {
								continueLoop = true; // already held, ignore
							}
							value = Press;
						} else {
							value = Release;
							if (heldInputs.contains(negative)) {
								keyCode = negative;
							} else if (heldInputs.contains(positive)) {
								keyCode = positive;
							} else {
								continueLoop = true; // continue cuz button was already released
							}
						}
					};

					switch (events[i].code) {
					case ABS_X: // left thumbstick x
						analyze4Directions(XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, CONTROLLER_LTHUMBSTICK_LEFT, CONTROLLER_LTHUMBSTICK_RIGHT);
						break;
					case ABS_Y: // left thumbstick y
						analyze4Directions(XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, CONTROLLER_LTHUMBSTICK_UP, CONTROLLER_LTHUMBSTICK_DOWN);
						break;
					case ABS_RX: // right thumbstick x
						analyze4Directions(XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE, CONTROLLER_RTHUMBSTICK_LEFT, CONTROLLER_RTHUMBSTICK_RIGHT);
						break;
					case ABS_RY: // right thumbstick y
						analyze4Directions(XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE, CONTROLLER_RTHUMBSTICK_UP, CONTROLLER_RTHUMBSTICK_DOWN);
						break;
					case ABS_HAT0X: // dpadx
						analyze4Directions(10, CONTROLLER_Left, CONTROLLER_Right);
						break;
					case ABS_HAT0Y: // dpady
						analyze4Directions(10, CONTROLLER_Up, CONTROLLER_Down);
						break;
					case ABS_Z:
						keyCode = CONTROLLER_LT;
						if (events[i].value > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
							value = Press;
						} else {
							value = Release;
						}
						break;
					case ABS_RZ:
						keyCode = CONTROLLER_RT;
						if (events[i].value > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
							value = Press;
						} else {
							value = Release;
						}
						break;
					}
					if (continueLoop) continue;
				}
				if (inputBinds[p1Jump].contains(keyCode)) input.inputType = PlayerButton::Jump;
				else if (inputBinds[p1Left].contains(keyCode)) input.inputType = PlayerButton::Left;
				else if (inputBinds[p1Right].contains(keyCode)) input.inputType = PlayerButton::Right;
				else {
					player1 = false;
					if (inputBinds[p2Jump].contains(keyCode)) input.inputType = PlayerButton::Jump;
					else if (inputBinds[p2Left].contains(keyCode)) input.inputType = PlayerButton::Left;
					else if (inputBinds[p2Right].contains(keyCode)) input.inputType = PlayerButton::Right;
					else continue;
				}
				if (value == Press) {
					if (heldInputs.contains(keyCode)) {
						continue; // already held, ignore
					} else {
						heldInputs.emplace(keyCode);
					}
				} else {
					if (!heldInputs.contains(keyCode)) {
						continue; // already released, ignore
					} else {
						heldInputs.erase(keyCode);
					}
				}
				break;
			}
			default:
				continue;
			}

			input.inputState = value;
			input.time = timestampFromLarge(events[i].time);
			input.isPlayer1 = player1;

			inputQueue.emplace_back(input);
		}
		ZeroMemory(events, sizeof(LinuxInputEvent[BUFFER_SIZE]));
		ReleaseMutex(hMutex);
	}
	else if (waitResult != WAIT_TIMEOUT) {
		cbf::log::error("WaitForSingleObject failed: %lu", GetLastError());
	}
}

void windowsSetup() {
	HANDLE gdMutex;

	HMODULE ntdll = GetModuleHandle("ntdll.dll");
	typedef void (*wine_get_host_version)(const char **sysname, const char **release);
	wine_get_host_version wghv = (wine_get_host_version)GetProcAddress(ntdll, "wine_get_host_version");
	if (wghv) { // if this function exists, the user is on Wine
		const char* sysname;
		const char* release;
		wghv(&sysname, &release);

		std::string sys = sysname;
		cbf::log::info("Wine %s", sys.c_str());

		if (sys == "Linux") settings::setBool("you-must-be-on-linux-to-change-this", true);
		if (sys == "Linux" && settings::getBool("wine-workaround", true)) { // background raw keyboard input doesn't work in Wine
			linuxNative = true;
			cbf::log::info("Linux native");

			hSharedMem = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(LinuxInputEvent[BUFFER_SIZE]), "LinuxSharedMemory");
			if (hSharedMem == NULL) {
				cbf::log::error("Failed to create file mapping: %lu", GetLastError());
				return;
			}

			pBuf = MapViewOfFile(hSharedMem, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(LinuxInputEvent[BUFFER_SIZE]));
			if (pBuf == NULL) {
				cbf::log::error("Failed to map view of file: %lu", GetLastError());
				CloseHandle(hSharedMem);
				return;
			}

			hMutex = CreateMutex(NULL, FALSE, "CBFLinuxMutex"); // used to gate access to the shared memory buffer for inputs
			if (hMutex == NULL) {
				cbf::log::error("Failed to create shared memory mutex: %lu", GetLastError());
				CloseHandle(hSharedMem);
				return;
			}

			gdMutex = CreateMutex(NULL, TRUE, "CBFWatchdogMutex"); // will be released when gd closes
			if (gdMutex == NULL) {
				cbf::log::error("Failed to create watchdog mutex: %lu", GetLastError());
				CloseHandle(hMutex);
				CloseHandle(hSharedMem);
				return;
			}

			SECURITY_ATTRIBUTES sa;
			sa.nLength = sizeof(SECURITY_ATTRIBUTES);
			sa.bInheritHandle = TRUE;
			sa.lpSecurityDescriptor = NULL;

			STARTUPINFO si;
			PROCESS_INFORMATION pi;
			ZeroMemory(&si, sizeof(si));
			si.cb = sizeof(si);
			ZeroMemory(&pi, sizeof(pi));

			char buffer[MAX_PATH];
			GetModuleFileNameA(g_module, buffer, MAX_PATH);
			std::string path = buffer;
			path = path.substr(0, path.find_last_of("\\/") + 1) + "linux-input.so";

			if (!CreateProcess(path.c_str(), NULL, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
				cbf::log::error("Failed to launch Linux input program: %lu", GetLastError());
				CloseHandle(hMutex);
				CloseHandle(gdMutex);
				CloseHandle(hSharedMem);
				return;
			}
		}
	}

	if (!linuxNative) {
		HANDLE process = GetCurrentProcess();
		std::thread raw(rawInputThread);
		DuplicateHandle(process, raw.native_handle(), process, &inputThreads[0], 0, FALSE, DUPLICATE_SAME_ACCESS);
		raw.detach();

		std::thread xinput(xinputThread);
		DuplicateHandle(process, xinput.native_handle(), process, &inputThreads[1], 0, FALSE, DUPLICATE_SAME_ACCESS);
		xinput.detach();
	}
}
