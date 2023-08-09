@ECHO OFF
SETLOCAL
CD /D %~dp0

SET gitexe="git.exe"
%gitexe% --version
IF /I %ERRORLEVEL%==0 GOTO :GitOK

SET gitexe="c:\Program Files\Git\cmd\git.exe"
IF NOT EXIST %gitexe% set gitexe="c:\Program Files\Git\bin\git.exe"
IF NOT EXIST %gitexe% GOTO :END

:GitOK

%gitexe% submodule update --init --recursive

:END
ENDLOCAL
EXIT /B
