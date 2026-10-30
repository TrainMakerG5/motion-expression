@echo off
setlocal
set VSLANG=1033
call "%~1" >nul
if errorlevel 1 exit /b 1
"%~2" -S "%~4" -B "%~4\build\ninja" -G Ninja "-DCMAKE_MAKE_PROGRAM=%~3" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b 1
"%~2" --build "%~4\build\ninja" --target Expression ExpressionEditor ExpressionProbe expression_tests expression_bridge_tests --parallel
if errorlevel 1 exit /b 1
"%~dp2ctest.exe" --test-dir "%~4\build\ninja" --output-on-failure
if errorlevel 1 exit /b 1
"%~2" --install "%~4\build\ninja" --prefix "%~4\dist"
exit /b %errorlevel%
