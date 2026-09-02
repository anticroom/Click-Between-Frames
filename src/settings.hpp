#pragma once

#include <string>

// values live in ClickBetweenFrames.ini next to the dll, read once on load
namespace settings {
	void load();

	bool getBool(const std::string& key, bool defaultValue);
	std::string getString(const std::string& key, const std::string& defaultValue);

	void setBool(const std::string& key, bool value); // calls save() once finished
	void save();

	void setSavedBool(const std::string& key, bool value);
}
