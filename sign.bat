@echo off
rem Re-sign build\ble_uart.exe with the self-signed certificate in certs\.
rem Usage: sign.bat
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\sign.ps1" %*
