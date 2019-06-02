@echo off
echo.
echo.
title Restore MPC VR default settings...
start /min reg delete "HKEY_CURRENT_USER\Software\MPC-BE Filters\MPC Video Renderer" /f
echo    settings were reset to default
echo.
pause >NUL
