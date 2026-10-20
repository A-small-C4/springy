@if not defined _echo echo off

rem Builds the standalone springy DLL. Usage: build.bat [Debug|Release]
rem (default Debug). Output: <this folder>\<Config>\springy.dll
rem
rem Self-contained: locates MSBuild with vswhere, no other repository needed.
rem YRpp is found via the YRPP_DIR environment variable if set, otherwise as a
rem sibling checkout at ..\..\YRpp (see springy.vcxproj).

setlocal
set CONFIG=%~1
if "%CONFIG%"=="" set CONFIG=Debug

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
	echo Could not find vswhere.exe. Install Visual Studio 2022 or Build Tools with the C++ workload.
	exit /b 1
)

set "MSBUILD="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do set "MSBUILD=%%i"

if not defined MSBUILD (
	echo Could not locate MSBuild.exe via vswhere.
	exit /b 1
)

"%MSBUILD%" "%~dp0springy.vcxproj" /p:Configuration=%CONFIG% /p:Platform=Win32 /m
endlocal
