# also check if preemptive_frames_enable = "true"
/opt/retroarch/bin/retroarch --config /dev/null -L v06x_libretro.so $1 -set "rewind_enable=false" --set "savestate_auto_load=false" --set "savestate_auto_save=false"
