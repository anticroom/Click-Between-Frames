#include "settings.hpp"
#include "log.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>

static std::map<std::string, std::string> g_values;
static std::string g_path;

static std::string trim(const std::string& s) {
	size_t start = s.find_first_not_of(" \t\r\n");
	if (start == std::string::npos) return "";
	size_t end = s.find_last_not_of(" \t\r\n");
	return s.substr(start, end - start + 1);
}

static const std::pair<const char*, const char*> DEFAULTS[] = {
	{ "soft-toggle", "false" },
	{ "safe-mode", "false" },
	{ "click-on-steps", "false" },
	{ "mouse-fix", "false" },
	{ "late-cutoff", "false" },
	{ "thread-priority", "false" },
	{ "wine-workaround", "true" },
	{ "right-click", "false" },
	{ "p1-jump-keys", "space" },
	{ "p2-jump-keys", "up" } // p2, and p1 jump keys for compatibility with customkeybind ports
};

extern HMODULE g_module;

void settings::load() {
	char path[MAX_PATH];
	GetModuleFileNameA(g_module, path, MAX_PATH);
	std::string dir = path;
	dir = dir.substr(0, dir.find_last_of("\\/") + 1);
	g_path = dir + "ClickBetweenFrames.ini";

	for (auto& [key, value] : DEFAULTS) g_values[key] = value;

	std::ifstream file(g_path);
	if (file) {
		std::string line;
		while (std::getline(file, line)) {
			if (line.empty() || line[0] == ';' || line[0] == '#' || line[0] == '[') continue;
			size_t eq = line.find('=');
			if (eq == std::string::npos) continue;
			g_values[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
		}
		return;
	}

	// write out a default config
	std::ofstream out(g_path);
	if (!out) return;
	out << "; Click Between Frames" << std::endl;
	for (auto& [key, value] : DEFAULTS) out << key << " = " << value << "\n";
}

bool settings::getBool(const std::string& key, bool defaultValue) {
	auto it = g_values.find(key);
	if (it == g_values.end()) return defaultValue;
	std::string value = it->second;
	std::transform(value.begin(), value.end(), value.begin(), ::tolower);
	return value == "true" || value == "1" || value == "yes";
}

std::string settings::getString(const std::string& key, const std::string& defaultValue) {
	auto it = g_values.find(key);
	return it == g_values.end() ? defaultValue : it->second;
}

void settings::setBool(const std::string& key, bool value) {
	g_values[key] = value ? "true" : "false";
}

void settings::save() {
	std::ofstream out(g_path);
	if (!out) {
		cbf::log::error("Could not write %s", g_path.c_str());
		return;
	}
	out << "; Click Between Frames 2.1" << std::endl;
	for (auto& [key, value] : g_values) out << key << " = " << value << std::endl;
}

void settings::setSavedBool(const std::string& key, bool value) {
	g_values[key] = value ? "true" : "false";
}
