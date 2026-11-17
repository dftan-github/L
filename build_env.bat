@echo off
rem Configure + build the L toolchain, and optionally an app, with the
rem MSVC/Windows SDK environment loaded. The plain shell lacks SDK include
rem paths, so clang cannot find <stdio.h> / <windows.h> until vcvars64.bat ran.
rem
rem   build_env.bat                 -> configure (if needed) + build l + l_runtime
rem   build_env.bat clean           -> wipe the build dir first
rem   build_env.bat app <dir>       -> build <dir>\main.l, then <dir>\check.l
rem                                    (e.g. build_env.bat app apps\seditor)
setlocal enabledelayedexpansion

set ROOT=%~dp0
set VCVARS=C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat
set BUILD=%ROOT%build\clang-debug
set LEXE=%BUILD%\compiler\l.exe

if not exist "%VCVARS%" (
  echo [ERROR] vcvars64.bat not found: %VCVARS%
  echo Edit VCVARS in this script to point at your Visual Studio install.
  exit /b 1
)

if /I "%1"=="clean" (
  echo [clean] removing %BUILD%
  if exist "%BUILD%" rmdir /s /q "%BUILD%"
)

rem Load the toolchain environment INTO this cmd process, then run cmake in it.
call "%VCVARS%" >nul
if errorlevel 1 (
  echo [ERROR] vcvars64.bat failed
  exit /b 1
)

echo [env] INCLUDE has %INCLUDE:~0,60%...

if not exist "%BUILD%\CMakeCache.txt" (
  echo [configure] %BUILD%
  cmake -S "%ROOT%." -B "%BUILD%" -G Ninja -DCMAKE_BUILD_TYPE=Debug
  if errorlevel 1 exit /b 1
)

echo [build] l_runtime + l
cmake --build "%BUILD%" --target l_runtime l
if errorlevel 1 (
  echo [ERROR] build failed
  exit /b 1
)

rem `app` is checked from a saved copy: the `clean` branch above rewrites %1.
set APP_ARG=%1
if /I "%APP_ARG%"=="clean" set APP_ARG=
if /I "%APP_ARG%"=="app" goto build_app
goto done_app

:build_app
if "%~2"=="" (
  echo [ERROR] usage: build_env.bat app ^<dir^>
  exit /b 1
)
set APPDIR=%ROOT%%~2
for %%I in ("%APPDIR%") do set APPLEAF=%%~nxI

if exist "%APPDIR%\main.l" (
  echo [app] %APPLEAF%.exe
  if exist "%APPDIR%\%APPLEAF%.exe" del /q "%APPDIR%\%APPLEAF%.exe"
  "%LEXE%" -O2 -o "%APPDIR%\%APPLEAF%.exe" "%APPDIR%\main.l"
  rem Verify by artifact, not errorlevel: a stale %errorlevel% from the
  rem preceding `if exist` / `echo` makes the exit-code check unreliable here.
  if not exist "%APPDIR%\%APPLEAF%.exe" (
    echo [ERROR] %APPLEAF% failed to build
    exit /b 1
  )
)
if exist "%APPDIR%\check.l" (
  echo [app] %APPLEAF%_check.exe
  if exist "%APPDIR%\%APPLEAF%_check.exe" del /q "%APPDIR%\%APPLEAF%_check.exe"
  "%LEXE%" -O2 -o "%APPDIR%\%APPLEAF%_check.exe" "%APPDIR%\check.l"
  if not exist "%APPDIR%\%APPLEAF%_check.exe" (
    echo [ERROR] %APPLEAF% check failed to build
    exit /b 1
  )
)

:done_app
echo [ok] %LEXE%
exit /b 0
