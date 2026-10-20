@if not defined _echo echo off

rem Copies the built DLL (and its PDB) next to gamemd.exe.
rem Usage: deploy.bat "<game folder>" [Debug|Release]
rem   or set RA2_DIR and call: deploy.bat "" Release

setlocal
set "GAMEDIR=%~1"
if "%GAMEDIR%"=="" set "GAMEDIR=%RA2_DIR%"
if "%GAMEDIR%"=="" (
	echo Usage: deploy.bat "path\to\game" [Debug^|Release]
	echo   or set RA2_DIR to the game folder.
	exit /b 1
)

set "CONFIG=%~2"
if "%CONFIG%"=="" set "CONFIG=Debug"

if not exist "%~dp0%CONFIG%\springy.dll" (
	echo Not found: %~dp0%CONFIG%\springy.dll - build it first: build.bat %CONFIG%
	exit /b 1
)

copy /y "%~dp0%CONFIG%\springy.dll" "%GAMEDIR%\springy.dll" >nul
if exist "%~dp0%CONFIG%\springy.pdb" copy /y "%~dp0%CONFIG%\springy.pdb" "%GAMEDIR%\springy.pdb" >nul
echo Deployed springy.dll (%CONFIG%) to "%GAMEDIR%".
endlocal
