@echo off
echo --------- Convert Amidar ---------
echo Converting Amidar
cd amidar
python ./amidar_rom_convert.py
cd ..
if errorlevel 1 goto :error

echo --- Success ---
goto end

:error
echo --- Error #%errorlevel%.
pause

:end
