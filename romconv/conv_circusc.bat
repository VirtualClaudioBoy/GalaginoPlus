@echo off
echo --------- Convert Circus Charlie ---------
echo Circus Charlie Unpack roms
python ./unpack.py circusc.zip
if errorlevel 1 goto :error

rem echo Circus Charlie Logo
rem python ./logoconv.py ../logos/circusc.png ../source/src/machines/circusc/circusc_logo.h
rem if errorlevel 1 goto :error

echo Converting Circus Charlie (tiles+sprites+palette+roms)
cd circusc
python circusc_rom_convert.py
cd ..

if errorlevel 1 goto :error

echo --- Success ---
goto end

:error
echo --- Error #%errorlevel%.
pause

:end
