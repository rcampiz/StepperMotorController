@echo off
REM Wrapper script that sets up MSVC environment and runs build

REM Setup VS 2017 environment (change this path if your VS is installed elsewhere)
if exist "F:\Program Files (x86)\Microsoft Visual Studio\2017\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" (
    call "F:\Program Files (x86)\Microsoft Visual Studio\2017\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
    if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%
)

REM Call the actual build script
call "%~dp0build.bat" %*
exit /b %ERRORLEVEL%