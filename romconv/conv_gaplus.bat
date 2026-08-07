@echo off
echo --------- Convert Gaplus ---------
echo Gaplus Unpack roms
python ./unpack.py gaplus.zip
if errorlevel 1 goto :error

echo Gaplus Logo
rem python ./logoconv.py ../logos/bnj.png ../source/src/machines/bnj/bnj_logo.h
rem if errorlevel 1 goto :error

echo Converting Gaplus (tiles+sprites+rom)
cd gaplus
python gaplus_rom_convert.py
cd ..

if errorlevel 1 goto :error

echo --- Success ---
goto end

:error
echo --- Error #%errorlevel%.
pause

:end
