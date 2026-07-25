@echo off
setlocal

if defined INCLUDE goto :build

call vcvars64 >nul
if errorlevel 1 exit /b %errorlevel%

:build
set "BOB_EXE=%~dp0tools\bob\bob.exe"
if not exist "%BOB_EXE%" (
	echo Orbiter's bundled Bob executable was not found at "%BOB_EXE%".
	exit /b 1
)

pushd "%~dp0"
"%BOB_EXE%" %*
set "BUILD_RESULT=%ERRORLEVEL%"
popd

exit /b %BUILD_RESULT%
