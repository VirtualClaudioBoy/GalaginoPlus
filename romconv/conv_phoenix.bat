@echo off
echo --------- Convert Phoenix ---------

rem echo Phoenix Logo
rem python ./logoconv.py ../logos/phoenix.png ../source/src/machines/phoenix/phoenix_logo.h
rem if errorlevel 1 goto :error

echo Converting Phoenix ROMs
cd phoenix
python romconv_phoenix.py
cd ..
if errorlevel 1 goto :error

echo --- Success ---
goto end

:error
echo --- Error #%errorlevel%.
pause

:end
