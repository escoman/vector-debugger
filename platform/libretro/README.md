v06x libretro core
==================

# Building

## Building for Windows

Under linux or wsl2, have gcc for w64-mingw32 installed.

```make platform=win CC=x86_64-w64-mingw32-gcc CXX=x86_64-w64-mingw32-g++```

The result is ```v06x_libretro.dll```.

## Building for R36S or RPi (32-bit)

Build on Raspberry Pi, or on any ARM Linux PC. Alternatively build on wsl2, just specify the right compiler.

```make platform=linux-portable V=1```

The result is `v06x_libretro.so`.

## Building for R36S or RPi (64-bit)

Cross-compile:
```
make platform=linux-portable-aarch64 CC=aarch64-linux-gnu-gcc CXX=aarch64-linux-gnu-g++ V=1 $*
```

Or natively on the device itself:
```
make platform=linux-portable-aarch64
```

## Building on Linux

```make V=1```

The result is `v06x_libretro.so`.

## Building on macOS (Apple Silicon)

```make ARCHFLAGS="-arch arm64" V=1```

# Installing

## Windows RetroArch

Put `v06x_libretro.dll` to `%RetroArch%\cores\`. Put `v06x_libretro.info` to `%RetroArch%\info`. 
You can install the dll from the GUI, but you must copy the `.info` file manually.

## SBC handhelds and RPi

### R36S with Arkos (32-bit)

There's an innumerable amount of variants of those things. I can only describe the one I've got, which seems to be a pretty vanilla R36S with Arkos.

1. Pick which SD card you want to use. I tend to use SD card 2, which is mounted in `/roms2`
2. Insert the card into card reader and make a folder `/vector06c/cores` in it. Copy some Vector-06C roms there too.
3. Put the card into R36S and turn it on.
3. In the list of displayed systems, enable RetroArch if you haven't done so previously: Press **SELECT** to get to the **MAIN MENU**. Navigate to **UI SETTINGS** -> **VISIBLE SYSTEMS**. Make sure that **RETROARCH** is checked.
3. Go to Options and start **File Manager**. 
4. Go to `/roms2/vector06c/cores` in the left panel. Go to `/home/ark/.config/retroarch32/cores` in the right one.
5. From the left panel, copy `v06x_libretro.so` and `v06x_libretro.info` to the right panel.

Exit the file manager. Find RetroArch in the game systems menu and launch `retroarch32` (not retroarch!).
There you'll find Vector-06C in the list of available cores. 

## R36S with Arkos (64-bit)

Refer to the 32-bit section but use aarch64 build and use retroarch without 32.

## ES-DE Flatpack on Linux

Create directory ~/ES-DE/custom_systems and create a file called es_systems.xml, or edit existing one:

```
<systemList>
  <system>
    <name>Vector-06c</name>
    <fullname>Vector-06c</fullname>
    <path>~/ROMs/vector06c</path>
    <extension>.rom .r0m .fdd .edd .wav</extension>
    <command label="v06x_libretro">%EMULATOR_RETROARCH% -L %CORE_RETROARCH%/v06x_libretro.so %ROM%</command>
    <platform>v06x</platform>
    <theme>v06x</theme>
  </system>
</systemList>
```

Put `v06x_libretro.so` to `~/.var/app/org.libretro.RetroArch/config/retroarch/cores/v06x_libretro.so`

Put Vector-06c roms to `~/ROMs/vector06c`, or your custom location.

Restart ES-DE, Vector-06C should be in the list of systems.

## macOS RetroArch

```
cp v06x_libretro.dylib ~/Library/Application\ Support/RetroArch/cores
cp v06x_libretro.info ~/Library/Application\ Support/RetroArch/info
```

# Troubleshooting

If the core appears named as `v06x_libretro` and not as `Vector-06C`, quit RetroArch and delete `core_info.cache`.

# Gamepad mapping

* D-pad, A/B is Joystick 1 + buttons
* X = ВК/Enter, Y = Space
* Left shoulder = РУС
* Left trigger = СС
* Right trigger = УС
* Right shoulder = ПС
* START = БЛК+СБР
* SELECT = F1

This is not sufficient for every game, unfortunately. For example some games want you to press ТАБ or АР2. These are not yet implemented.


