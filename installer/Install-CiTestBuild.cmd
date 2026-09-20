@echo off
setlocal

if not exist "%~dp0MetasequoiaImeLocalTest.cer" (
  echo Test certificate not found.
  pause
  exit /b 1
)

for %%F in ("%~dp0MetasequoiaIME_Setup_*.exe") do if exist "%%~fF" set "installer=%%~fF"
if not defined installer (
  echo Installer not found.
  pause
  exit /b 1
)

set "MSIME_TEST_CERT=%~dp0MetasequoiaImeLocalTest.cer"
set "MSIME_TEST_EXE=%installer%"
powershell.exe -NoProfile -Command "$cert=[Security.Cryptography.X509Certificates.X509Certificate2]::new($env:MSIME_TEST_CERT); $signed=[Security.Cryptography.X509Certificates.X509Certificate2]::new([Security.Cryptography.X509Certificates.X509Certificate]::CreateFromSignedFile($env:MSIME_TEST_EXE)); if ($cert.Thumbprint -ne $signed.Thumbprint -or $cert.Subject -ne 'CN=Metasequoia IME Local Test Code Signing') { throw 'Certificate does not match installer signature' }"
if errorlevel 1 (
  echo Certificate verification failed. Nothing was trusted or installed.
  pause
  exit /b 1
)

fltmc >nul 2>&1
if errorlevel 1 (
  powershell.exe -NoProfile -Command "Start-Process -Verb RunAs -FilePath '%~f0'"
  exit /b
)

certutil.exe -addstore -f Root "%MSIME_TEST_CERT%" || exit /b 1
certutil.exe -addstore -f TrustedPublisher "%MSIME_TEST_CERT%" || exit /b 1
start "" /wait "%installer%"
