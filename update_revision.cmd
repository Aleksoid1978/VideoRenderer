@ECHO OFF
SETLOCAL
CD /D %~dp0

ECHO #pragma once > revision.h

SET gitexe="git.exe"
IF NOT EXIST %gitexe% set gitexe="c:\Program Files\Git\bin\git.exe"
IF NOT EXIST %gitexe% set gitexe="c:\Program Files\Git\cmd\git.exe"

IF NOT EXIST %gitexe% GOTO END

%gitexe% log -1 --date=format:%%Y.%%m.%%d --pretty=format:"#define MPCVR_REV_DATE %%ad%%n" >> revision.h
%gitexe% log -1 --pretty=format:"#define MPCVR_REV_HASH %%h%%n" >> revision.h

<nul set /p strTemp=#define MPCVR_REV_NUM >> revision.h
%gitexe% rev-list --all --count >> revision.h
IF %ERRORLEVEL% NEQ 0 (
ECHO 0 >> revision.h
)

:END
ENDLOCAL
EXIT /B
