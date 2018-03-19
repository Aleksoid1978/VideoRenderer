REM @ECHO OFF
CD /D %~dp0

SET "MSBUILD_SWITCHES=/nologo /consoleloggerparameters:Verbosity=minimal /maxcpucount /nodeReuse:true"
SET "BUILDTYPE=Build"
SET "BUILDCFG="Release Filter""

CALL :SubVSPath
SET "TOOLSET=%VS_PATH%\Common7\Tools\vsdevcmd"
SET VS_PATH
SET TOOLSET

SET "LOG_DIR=bin\logs"
IF NOT EXIST "%LOG_DIR%" MD "%LOG_DIR%"

CALL "%TOOLSET%" -no_logo -arch=x86
REM again set the source directory (fix possible bug in VS2017)
CD /D %~dp0
CALL :SubMPCVR x86

CALL "%TOOLSET%" -no_logo -arch=amd64
REM again set the source directory (fix possible bug in VS2017)
CD /D %~dp0
CALL :SubMPCVR x64

TITLE Compiling MPC Video Renderer [FINISHED]
TIMEOUT /T 3
ENDLOCAL
EXIT

:SubVSPath
FOR /f "delims=" %%A IN ('"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -property installationPath -latest -requires Microsoft.Component.MSBuild') DO SET VS_PATH=%%A
EXIT /B

:SubMPCVR
TITLE Compiling MPC Video Renderer - %BUILDCFG%^|%1...
MSBuild.exe MpcVideoRenderer.sln %MSBUILD_SWITCHES%^
 /target:%BUILDTYPE% /p:Configuration=%BUILDCFG% /p:Platform=%1^
 /flp1:LogFile=%LOG_DIR%\errors_%BUILDCFG%_%1.log;errorsonly;Verbosity=diagnostic^
 /flp2:LogFile=%LOG_DIR%\warnings_%BUILDCFG%_%1.log;warningsonly;Verbosity=diagnostic
IF %ERRORLEVEL% NEQ 0 (
  CALL :SubMsg "ERROR" "MpcVideoRenderer.sln %BUILDCFG% %1 - Compilation failed!"
) ELSE (
  CALL :SubMsg "INFO" "MpcVideoRenderer.sln %BUILDCFG% %1 compiled successfully"
)
EXIT /B

:SubMsg
ECHO. & ECHO ------------------------------
IF /I "%~1" == "ERROR" (
  CALL :SubColorText "0C" "[%~1]" & ECHO  %~2
) ELSE IF /I "%~1" == "INFO" (
  CALL :SubColorText "0A" "[%~1]" & ECHO  %~2
) ELSE IF /I "%~1" == "WARNING" (
  CALL :SubColorText "0E" "[%~1]" & ECHO  %~2
)
ECHO ------------------------------ & ECHO.
IF /I "%~1" == "ERROR" (
  ECHO Press any key to close this window...
  PAUSE >NUL
  ENDLOCAL
  EXIT
) ELSE (
  EXIT /B
)

:SubColorText
FOR /F "tokens=1,2 delims=#" %%A IN (
  '"PROMPT #$H#$E# & ECHO ON & FOR %%B IN (1) DO REM"') DO (
  SET "DEL=%%A")
<NUL SET /p ".=%DEL%" > "%~2"
FINDSTR /v /a:%1 /R ".18" "%~2" NUL
DEL "%~2" > NUL 2>&1
EXIT /B