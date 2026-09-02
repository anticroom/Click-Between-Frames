#define _CRT_SECURE_NO_WARNINGS
#include "log.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>

extern HMODULE g_module;

static std::mutex logLock;

static void writeLog(const char* level, const char* fmt, va_list args) {
	char message[1024];
	vsnprintf(message, sizeof(message), fmt, args);

	char line[1152];
	snprintf(line, sizeof(line), "[CBF] [%s] %s\n", level, message);

	OutputDebugStringA(line);

	static std::string path = []() {
		char buffer[MAX_PATH];
		GetModuleFileNameA(g_module, buffer, MAX_PATH);
		std::string dir = buffer;
		return dir.substr(0, dir.find_last_of("\\/") + 1) + "ClickBetweenFrames.log";
	}();

	std::lock_guard lock(logLock);
	if (FILE* file = fopen(path.c_str(), "a")) {
		fputs(line, file);
		fclose(file);
	}
}

void cbf::log::info(const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
	writeLog("info", fmt, args);
	va_end(args);
}

void cbf::log::debug(const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
	writeLog("debug", fmt, args);
	va_end(args);
}

void cbf::log::error(const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
	writeLog("error", fmt, args);
	va_end(args);
}
