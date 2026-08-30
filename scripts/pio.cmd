@echo off
setlocal

set "PIO_EXE=%USERPROFILE%\.platformio\penv\Scripts\pio.exe"

if not exist "%PIO_EXE%" (
    >&2 echo PlatformIO Core was not found at "%PIO_EXE%". Install the PlatformIO IDE extension, or run "pio" / "python -m platformio" directly if Core is on PATH.
    exit /b 1
)

"%PIO_EXE%" %*
exit /b %ERRORLEVEL%
