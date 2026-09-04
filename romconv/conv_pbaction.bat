@echo off
echo --------- Convert Pinball Action ---------
echo Pinball Action Unpack roms
python ./unpack.py pbaction.zip
if errorlevel 1 goto :error

rem echo Pinball Action Logos
rem python ./logoconv.py ../logos/pbaction.png ../source/src/machines/pbaction/pbaction_logo.h
if errorlevel 1 goto :error

echo Converting Pinball Action
cd pbaction
python pbaction_rom_convert.py
set "convert_error=%errorlevel%"
cd ..
if not "%convert_error%"=="0" goto :error

echo --- Success ---
goto end

:error
echo --- Error #%errorlevel%.
pause

:end
