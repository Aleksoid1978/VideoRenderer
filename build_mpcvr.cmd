@ECHO OFF
REM (C) 2018 see Authors.txt
REM
REM This file is part of MPC-BE.
REM
REM MPC-BE is free software; you can redistribute it and/or modify
REM it under the terms of the GNU General Public License as published by
REM the Free Software Foundation; either version 3 of the License, or
REM (at your option) any later version.
REM
REM MPC-BE is distributed in the hope that it will be useful,
REM but WITHOUT ANY WARRANTY; without even the implied warranty of
REM MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
REM GNU General Public License for more details.
REM
REM You should have received a copy of the GNU General Public License
REM along with this program.  If not, see <http://www.gnu.org/licenses/>.

CD /D %~dp0

SET "MSBUILD_SWITCHES=/nologo /consoleloggerparameters:Verbosity=minimal /maxcpucount /nodeReuse:true"
SET "BUILDTYPE=Build"
SET "BUILDCFG=Release"
SET "PCKG_NAME=MPCVideoRenderer"

CALL :SubVSPath
SET "TOOLSET=%VS_PATH%\Common7\Tools\vsdevcmd"

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

CALL :SubDetectSevenzipPath
IF DEFINED SEVENZIP (
    IF EXIST "bin\%PCKG_NAME%.zip" DEL "bin\%PCKG_NAME%.zip"
    IF EXIST "bin\%PCKG_NAME%"     RD /Q /S "bin\%PCKG_NAME%"
    TITLE Copying %PCKG_NAME%...
    IF NOT EXIST "bin\%PCKG_NAME%" MD "bin\%PCKG_NAME%"
    COPY /Y /V "bin\Filters_x86\MpcVideoRenderer.ax"   "bin\%PCKG_NAME%\MpcVideoRenderer.ax" >NUL
    COPY /Y /V "bin\Filters_x64\MpcVideoRenderer64.ax" "bin\%PCKG_NAME%\MpcVideoRenderer64.ax" >NUL
    COPY /Y /V "distrib\Install_MPCVR_32.cmd"          "bin\%PCKG_NAME%\Install_MPCVR_32.cmd" >NUL
    COPY /Y /V "distrib\Install_MPCVR_64.cmd"          "bin\%PCKG_NAME%\Install_MPCVR_64.cmd" >NUL	
    COPY /Y /V "distrib\Uninstall_MPCVR_32.cmd"        "bin\%PCKG_NAME%\Uninstall_MPCVR_32.cmd" >NUL
    COPY /Y /V "distrib\Uninstall_MPCVR_64.cmd"        "bin\%PCKG_NAME%\Uninstall_MPCVR_64.cmd" >NUL
	COPY /Y /V "LICENSE"                               "bin\%PCKG_NAME%\LICENSE" >NUL

    TITLE Creating archive %PCKG_NAME%.zip...
    START "7z" /B /WAIT "%SEVENZIP%" a -tzip -mx9 "bin\%PCKG_NAME%.zip" ".\bin\%PCKG_NAME%\*"
    IF %ERRORLEVEL% NEQ 0 CALL :SubMsg "ERROR" "Unable to create %PCKG_NAME%.zip!"
    CALL :SubMsg "INFO" "%PCKG_NAME%.zip successfully created"
)

TITLE Compiling MPC Video Renderer [FINISHED]
TIMEOUT /T 3
ENDLOCAL
EXIT

:SubVSPath
FOR /f "delims=" %%A IN ('"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -property installationPath -latest -requires Microsoft.Component.MSBuild') DO SET VS_PATH=%%A
EXIT /B

:SubDetectSevenzipPath
FOR %%G IN (7z.exe) DO (SET "SEVENZIP_PATH=%%~$PATH:G")
IF EXIST "%SEVENZIP_PATH%" (SET "SEVENZIP=%SEVENZIP_PATH%" & EXIT /B)

FOR %%G IN (7za.exe) DO (SET "SEVENZIP_PATH=%%~$PATH:G")
IF EXIST "%SEVENZIP_PATH%" (SET "SEVENZIP=%SEVENZIP_PATH%" & EXIT /B)

FOR /F "tokens=2*" %%A IN (
  'REG QUERY "HKLM\SOFTWARE\7-Zip" /v "Path" 2^>NUL ^| FIND "REG_SZ" ^|^|
   REG QUERY "HKLM\SOFTWARE\Wow6432Node\7-Zip" /v "Path" 2^>NUL ^| FIND "REG_SZ"') DO SET "SEVENZIP=%%B\7z.exe"
EXIT /B

:SubMPCVR
TITLE Compiling MPC Video Renderer - %BUILDCFG%^|%1...
MSBuild.exe MpcVideoRenderer.sln %MSBUILD_SWITCHES%^
 /target:%BUILDTYPE% /p:Configuration="%BUILDCFG%" /p:Platform=%1^
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