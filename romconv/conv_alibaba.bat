@echo off
echo --------- Convert Alibaba ---------
echo Alibaba Unpack roms
python ./unpack.py alibaba.zip
if errorlevel 1 goto :error

rem echo Alibaba Logos
rem python ./logoconv.py ../logos/alibaba.png ../source/src/machines/alibaba/alibaba_logo.h
rem if errorlevel 1 goto :error

cd alibaba
python ./alibaba_rom_convert.py
cd ..
if errorlevel 1 goto :error

echo --- Success ---
goto end

:error
echo --- Error #%errorlevel%.
pause

:end
