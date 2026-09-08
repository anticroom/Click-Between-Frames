# Installation

This is a Click Between Frames port to GD 2.113. If you're using Megahack with your 2.1 install, place our DLL from releases into your /extensions/ folder to load on launch.

# What does it do

This mod allows your inputs to register in between visual frames, which drastically increases input precision on low framerates like 60FPS.

If you like the mod, please consider [donating](https://www.paypal.com/donate/?hosted_button_id=U2LWN9H395TF8)! to the original CBF developer, this port doesn't take anything for itself. But if you would like to support me ontop of Sizzy too, then please [donate](https://www.denouementdemonlist.com/donate) to me as well! Any support to any of us is greatly appreciated.

# How to use

If the icon is automatically jumping when you respawn, disable the "Stop Triggers on Death" hack in Mega Hack.

Physics Bypass isn't included in this port.

If on Linux, and the mod doesn't work, please try running the command <cr>sudo usermod -aG input $USER</c> (this will make your system slightly less secure).

Logs get written to `ClickBetweenFrames.log` next to the dll. For issue reporting, please attach the log in your bug report.

# Known issues

- This mod does not work with bots
- Linux controller support is experimental

# Compiling

If for some reason you want to compile this yourself, you'll need Git, CMake and MSVC (32-bit target, since GD is a 32-bit game). Clone the repo recursively (`--recursive`), then just run `build.bat`.

# Credits

Full Credit to the contributors of the orignal [Click Between Frames](https://github.com/theyareonit/Click-Between-Frames) repository for their original work copied for this port to be possible.

Icon by alex/sincos.

Android, macOS, & iOS code based off [Click on Steps by zmx](https://github.com/qimiko/click-on-steps), used with permission. \
Android port by [mat](https://github.com/matcool), macOS & iOS ports by [Jasmine](https://github.com/hiimjasmine00).

(these credits are slightly outdated since the mod just uses the vanilla game's input handling now but oh well)