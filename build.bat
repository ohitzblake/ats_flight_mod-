@echo off
echo Building ATS Flight Mode...

mkdir build
cd build

cmake .. -G "Visual Studio 16 2019" -A x64
cmake --build . --config Release

echo Build complete! Check the bin folder for the plugin.
pause
