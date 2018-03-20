@cd /d "%~dp0"
@regsvr32.exe MpcVideoRenderer.ax /u /s
@if %errorlevel% NEQ 0 goto error
@if not exist "%SystemRoot%\SysWOW64\cmd.exe" goto success
@regsvr32.exe MpcVideoRenderer64.ax /u /s
:success
@echo.
@echo.
@echo    Uninstallation succeeded.
@echo.
@goto done
:error
@echo.
@echo.
@echo    Uninstallation failed.
@echo.
@echo    You need to right click "Uninstall_MPCVR.bat" and choose "run as admin".
@echo.
:done
@pause >NUL
