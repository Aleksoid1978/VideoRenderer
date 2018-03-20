@cd /d "%~dp0"
@regsvr32.exe MpcVideoRenderer.ax /s
@if %errorlevel% NEQ 0 goto error
@if not exist "%SystemRoot%\SysWOW64\cmd.exe" goto success
@regsvr32.exe MpcVideoRenderer64.ax /s
:success
@echo.
@echo.
@echo    Installation succeeded.
@echo.
@echo    Please do not delete the MpcVideoRenderer folder.
@echo    The installer has not copied the files anywhere.
@echo.
@goto done
:error
@echo.
@echo.
@echo    Installation failed.
@echo.
@echo    You need to right click "Install_MPCVR.bat" and choose "run as admin".
@echo.
:done
@pause >NUL
