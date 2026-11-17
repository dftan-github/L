@echo off
rem Build the L compiler first (cmake), then compile SEditor against it.
rem   build.bat            -> seditor.exe
rem   build.bat check      -> seditor_check.exe (tree round-trip, no window)
setlocal
set ROOT=%~dp0..\..
set L=%ROOT%\build\compiler\l.exe
if not exist "%L%" (
  echo [ERROR] compiler not found: %L%
  echo Build it first:  cmake --build build --target l
  exit /b 1
)

if /I "%1"=="check" (
  "%L%" -O2 -o "%~dp0seditor_check.exe" "%~dp0check.l"
) else (
  "%L%" -O2 -o "%~dp0seditor.exe" "%~dp0main.l"
)
exit /b %ERRORLEVEL%
