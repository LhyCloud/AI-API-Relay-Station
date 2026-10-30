@echo off
setlocal
set "VS=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if not exist build mkdir build
if not exist dist mkdir dist
cl /nologo /std:c++17 /O2 /MT /EHsc /DNDEBUG /D_CRT_SECURE_NO_WARNINGS /utf-8 /W3 /Fobuild\ /Fe:build\ai-relay.exe src\*.cpp /link ws2_32.lib winhttp.lib shell32.lib
if errorlevel 1 exit /b 1
copy /y build\ai-relay.exe dist\ai-relay.exe >nul 2>&1
if errorlevel 1 (
  echo [build] OK -^> build\ai-relay.exe ^(dist copy skipped: file in use^)
) else (
  echo [build] OK -^> dist\ai-relay.exe
)
