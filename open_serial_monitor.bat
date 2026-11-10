@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0monitor.ps1"
if %ERRORLEVEL% neq 0 pause
