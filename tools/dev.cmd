@echo off
rem tools\dev.ps1 without the execution-policy prompt: runs on machines where PowerShell
rem scripts are blocked (the default policy, Restricted). Same steps and options, e.g.
rem   tools\dev.cmd build deploy run-dist
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0dev.ps1" %*
exit /b %ERRORLEVEL%
