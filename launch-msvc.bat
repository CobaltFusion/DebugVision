@echo off
REM script to be used on Windows to generate and open a VC++ solution

REM where to put the build files
set BUILDDIR=%~dp0build

REM the VC++ solution file
set SLNFILE=%BUILDDIR%\visual-studio\DebugVision.sln

REM make Qt visible
set QTDIR=C:\Qt\6.2.2\msvc2019_64
if not exist %QTDIR% (echo invalid Qt bin path: "%QTDIR%" && GOTO:FAILURE)
set PATH=%QTDIR%\bin;%PATH%

REM find VC++
set "VCPATH=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community"
if not exist "%VCPATH%" (
    set "VCPATH=C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional"
)

REM generate build files if not already done
if not exist %SLNFILE% (
    REM make VC++ & cmake visible
    REM make sure to install "C++ CMake tools for Windows" in VS installer
    call "%VCPATH%\VC\Auxiliary\Build\vcvars64.bat" || GOTO:FAILURE

    if exist %BUILDDIR% rmdir /s/q %BUILDDIR%
    mkdir %BUILDDIR% || GOTO:FAILURE
    compact /c /q %BUILDDIR% > nul
    cmake --preset="visual-studio" || GOTO:FAILURE
)

REM open the VC++ solution
start %SLNFILE%

REM success!
GOTO:EOF

:FAILURE
echo Failure!
pause