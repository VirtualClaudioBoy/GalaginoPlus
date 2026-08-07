@echo off
echo --------- Convert Mappy ---------
echo Mappy Unpack roms
python ./unpack.py mappy.zip
if errorlevel 1 goto :error

rem echo Mappy Logo
rem python ./logoconv.py ../logos/mappy.png ../source/src/machines/mappy/mappy_logo.h
rem if errorlevel 1 goto :error

echo Converting Mappy (tiles+sprites+palette+roms+wavetable, con autotest)
cd mappy
python mappy_rom_convert.py
cd ..

if errorlevel 1 goto :error

echo --- Success ---
goto end

:error
echo --- Error #%errorlevel%.
pause

:end
