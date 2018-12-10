@cd /d "%~dp0"
@regsvr32.exe MpcVideoRenderer64.ax /u /s
@if %errorlevel% NEQ 0 goto error
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
@echo    You need to right click "Uninstall_MPCVR_64.bat" and choose "run as admin".
@echo.
:done
@pause >NUL
