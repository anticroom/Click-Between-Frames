#pragma once

// writes next to the dll and to the debugger
// nested in a namespace because the crt already has a global `log`
namespace cbf {
	namespace log {
		void info(const char* fmt, ...);
		void debug(const char* fmt, ...);
		void error(const char* fmt, ...);
	}
}
