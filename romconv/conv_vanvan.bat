@echo off
echo --------- Convert Van Van Car ---------
echo Van Van Car Unpack roms
python ./unpack.py vanvan.zip
if errorlevel 1 goto :error

echo Van Van Car CPU code (main bank 0x0000-0x3fff)
python ./romconv.py vanvan_rom ./roms/van-1.50 ./roms/van-2.51 ./roms/van-3.52 ./roms/van-4.53 ../source/src/machines/vanvan/vanvan_rom.h
if errorlevel 1 goto :error

echo Van Van Car CPU code (extra bank 0x8000-0x8fff)
python ./romconv.py vanvan_rom2 ./roms/van-5.39 ../source/src/machines/vanvan/vanvan_rom2.h
if errorlevel 1 goto :error

echo Van Van Car Tiles
python ./tileconv.py vanvan_tilemap ./roms/van-20.18 ../source/src/machines/vanvan/vanvan_tilemap.h
if errorlevel 1 goto :error

echo Van Van Car Sprites
python ./spriteconv.py vanvan_sprites pacman ./roms/van-21.19 ../source/src/machines/vanvan/vanvan_spritemap.h
if errorlevel 1 goto :error

echo Van Van Car Colormaps
python ./cmapconv.py vanvan_colormap ./roms/6331-1.6 0 ./roms/6301-1.37 ../source/src/machines/vanvan/vanvan_cmap.h
if errorlevel 1 goto :error

echo --- Success ---
goto end

:error
echo --- Error #%errorlevel%.
pause

:end
