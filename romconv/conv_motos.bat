@echo off
echo --------- Convert Motos ---------
echo Motos Unpack roms
python ./unpack.py motos.zip
if errorlevel 1 goto :error

rem echo Motos Logo
rem python ./logoconv.py ../logos/motos.png ../source/src/machines/motos/motos_logo.h
rem if errorlevel 1 goto :error

echo Converting Motos (tiles+sprites+palette+roms+wavetable)
cd motos
python motos_rom_convert.py
cd ..

if errorlevel 1 goto :error

echo --- Success ---
goto end

:error
echo --- Error #%errorlevel%.
pause

:end
