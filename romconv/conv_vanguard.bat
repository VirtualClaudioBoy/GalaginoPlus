@echo off
setlocal
pushd "%~dp0"

echo --------- Convert Vanguard ---------
echo Vanguard Unpack roms
python ./unpack.py vanguard.zip
if errorlevel 1 goto :error

echo Vanguard Logo
python ./logoconv.py ../logos/vanguard.png ../source/src/machines/vanguard/vanguard_logo.h
if errorlevel 1 goto :error

echo Converting Vanguard ROMs
pushd vanguard
python vanguard_rom_convert.py
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
