@echo off
echo --------- Convert Burger Time ---------
echo Burger Time Unpack roms
python ./unpack.py btime.zip
if errorlevel 1 goto :error

rem echo Burger Time Logo
rem python ./logoconv.py ../logos/btime.png ../source/src/machines/btime/btime_logo.h
rem if errorlevel 1 goto :error

echo Converting Burger Time (tiles+sprites+rom)
cd btime
python btime_rom_convert.py
cd ..

if errorlevel 1 goto :error

echo --- Success ---
goto end

:error
echo --- Error #%errorlevel%.
pause

:end
