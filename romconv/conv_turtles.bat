@echo off
echo --------- Convert Turtles ---------
echo Converting Turtles
cd turtles
python ./turtles_rom_convert.py
cd ..
if errorlevel 1 goto :error

echo --- Success ---
goto end

:error
echo --- Error #%errorlevel%.
pause

:end
