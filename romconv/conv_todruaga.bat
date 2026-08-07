@echo off
echo --------- Convert The Tower of Druaga ---------
echo Tower of Druaga Unpack roms
python ./unpack.py todruaga.zip
if errorlevel 1 goto :error

rem echo Tower of Druaga Logo
rem python ./logoconv.py ../logos/todruaga.png ../source/src/machines/todruaga/todruaga_logo.h
rem if errorlevel 1 goto :error

echo Converting Tower of Druaga (tiles+sprites+palette+roms+wavetable, con autotest)
cd todruaga
python todruaga_rom_convert.py
cd ..

if errorlevel 1 goto :error

echo --- Success ---
goto end

:error
echo --- Error #%errorlevel%.
pause

:end
