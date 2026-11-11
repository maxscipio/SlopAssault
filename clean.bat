@echo off
setlocal
cd /d "%~dp0"
rem Removes everything the build and the game generate, so the next build.bat starts from nothing:
rem   out\         build tree, compiled game code, settings, logs, saves, shader cache
rem   generated\   code recompiled from default.xbe
rem   *.zip        what release.bat packs
rem "clean.bat all" also empties assets\, which holds the game files you supplied.
rem The SDK, patches\, config\ and src\ are never touched.
if exist out rd /s /q out
if exist generated rd /s /q generated
del /q MechAssault.zip 2>nul
if /i "%~1"=="all" (
    if exist assets for /d %%d in (assets\*) do rd /s /q "%%d"
    if exist assets del /q assets\* 2>nul
)
echo Cleaned.
if /i not "%~1"=="all" echo The game files in assets\ were kept. Run "clean.bat all" to remove them too.
