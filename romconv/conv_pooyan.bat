@echo off
echo --------- Convert Pooyan ---------
echo Pooyan Unpack roms
python ./unpack.py pooyan.zip
if errorlevel 1 goto :error

rem echo Pooyan Logos
rem python ./logoconv.py ../logos/pooyan.png ../source/src/machines/pooyan/pooyan_logo.h
rem if errorlevel 1 goto :error

echo Converting Pooyan
cd pooyan
python pooyan_rom_convert.py
cd ..
if errorlevel 1 goto :error

echo --- Success ---
goto end

:error
echo --- Error #%errorlevel%.
pause

:end
