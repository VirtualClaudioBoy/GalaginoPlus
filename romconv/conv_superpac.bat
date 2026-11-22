@echo off
echo --------- Convert Super Pac-Man ---------
echo Super Pac-Man Unpack roms
python ./unpack.py superpac.zip
if errorlevel 1 goto :error

rem echo Super Pac-Man Logo
rem python ./logoconv.py ../logos/superpac.png ../source/src/machines/superpac/superpac_logo.h
rem if errorlevel 1 goto :error

echo Converting Super Pac-Man (tiles+sprites+palette+roms+wavetable)
cd superpac
python superpac_rom_convert.py
cd ..

if errorlevel 1 goto :error

echo --- Success ---
goto end

:error
echo --- Error #%errorlevel%.
pause

:end
