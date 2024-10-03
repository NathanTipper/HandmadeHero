@echo off

set CommonCompilerFlags=-nologo -GR- -Oi -WX -W4 -wd4201 -wd4100 -wd4189 -DHANDMADE_INTERNAL=1 -DHANDMADE_SLOW=1 -DHANDMADE_WINDOWS=1 -FC -Z7 -Fm
set CommonLinkerFlags=-opt:ref user32.lib gdi32.lib winmm.lib
set dt=%DATE:~6,4%_%DATE:~3,2%_%DATE:~0,2%__%TIME:~0,2%_%TIME:~3,2%_%TIME:~6,2%
set dt=%dt: =0%
IF NOT EXIST build mkdir build
pushd build

REM 32-bit build
REM cl %CommonCompilerFlags% .\src\win32_HandmadeHero.cpp /link -subsystem:windows,5.1 %CommonLinkerFlags%

REM 64-bit build
del *.pdb > NUL 2> NUL
cl %CommonCompilerFlags% ..\src\handmade.cpp /LD /link /EXPORT:GameUpdateAndRender /EXPORT:GameGetSoundSamples -incremental:no /PDB:handmade_%dt%.pdb 
cl %CommonCompilerFlags% ..\src\win32_HandmadeHero.cpp /link %CommonLinkerFlags%
popd
