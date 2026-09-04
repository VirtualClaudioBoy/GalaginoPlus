@echo off
setlocal
pushd "%~dp0"

echo --------- Convert Fantasy ---------
python .\logoconv.py ..\logos\fantasy.png ..\source\src\machines\fantasy\fantasy_logo.h
if errorlevel 1 goto :error

pushd fantasy
python fantasy_rom_convert.py
set "convert_error=%errorlevel%"
popd
if not "%convert_error%"=="0" goto :error

echo --- Success ---
popd
endlocal
exit /b 0

:error
echo --- Error #%errorlevel%.
popd
endlocal
exit /b 1
