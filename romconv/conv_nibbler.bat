@echo off
setlocal
pushd "%~dp0"

echo --------- Convert Nibbler ---------
python .\logoconv.py ../logos/nibbler.png ../source/src/machines/nibbler/nibbler_logo.h
if errorlevel 1 goto :error

pushd nibbler
python nibbler_rom_convert.py
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
