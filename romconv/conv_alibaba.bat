@echo off
echo --------- Convert Alibaba ---------
echo Converting Alibaba
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
