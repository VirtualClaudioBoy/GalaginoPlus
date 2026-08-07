@echo off
echo --------- Convert Bump'n'Jump ---------
echo Bump'n'Jump Unpack roms
python ./unpack.py bnj.zip
if errorlevel 1 goto :error

rem echo Bump'n'Jump Logo
rem python ./logoconv.py ../logos/bnj.png ../source/src/machines/bnj/bnj_logo.h
rem if errorlevel 1 goto :error

echo Converting Bump'n'Jump (tiles+sprites+rom)
cd bnj
python bnj_rom_convert.py
cd ..

if errorlevel 1 goto :error

echo --- Success ---
goto end

:error
echo --- Error #%errorlevel%.
pause

:end
