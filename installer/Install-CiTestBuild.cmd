@echo off
setlocal

fltmc >nul 2>&1
if errorlevel 1 (
  powershell.exe -NoProfile -Command "Start-Process -Verb RunAs -FilePath '%~f0'"
  exit /b
)

if not exist "%~dp0MetasequoiaImeLocalTest.cer" (
  echo Test certificate not found.
  pause
  exit /b 1
)

certutil.exe -addstore -f Root "%~dp0MetasequoiaImeLocalTest.cer" || exit /b 1
certutil.exe -addstore -f TrustedPublisher "%~dp0MetasequoiaImeLocalTest.cer" || exit /b 1

for %%F in ("%~dp0MetasequoiaIME_Setup_*.exe") do if exist "%%~fF" set "installer=%%~fF"
if not defined installer (
  echo Installer not found.
  pause
  exit /b 1
)

start "" /wait "%installer%"
