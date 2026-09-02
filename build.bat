@echo off
REM I'm just not going to bother retyping the build command every local dev build sooooo this basic peice of shit will work :D
cmake -B build -A win32 -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build --config Release
