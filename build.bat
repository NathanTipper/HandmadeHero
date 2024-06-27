@echo off

rmdir /s build
mkdir build
pushd build
cl -Zi ..\src\win32_HandmadeHero.cpp user32.lib gdi32.lib
popd
