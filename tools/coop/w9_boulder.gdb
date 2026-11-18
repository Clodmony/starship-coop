# Is Titania's boulder site hit (#232, #209's `args` site)? One line each time a shot boulder breaks into four:
# Titania_TiBoulder_Spawn2 (src/overlays/ovl_i5/fox_ti.c) calls Titania_TiBoulder_Spawn with three random draws in
# three arguments, whose order C leaves to the compiler. For
# `python tools/coop/w7asan_sweep.py sweep --levels titania --exe <a plain build>/Starship --gdb tools/coop/w9_boulder.gdb`,
# which counts the lines. The breaks are on function names, so an edit of the file does not move them.
set pagination off
set confirm off
set print thread-events off
set breakpoint pending on
handle SIGPIPE nostop noprint pass

# A boulder of full size was shot to pieces and drops no ring: four small ones fly off.
break Titania_TiBoulder_Spawn2
commands
silent
printf "w7trace boulder-break ok=1 frame=%d actor=%d\n", gGameFrameCount, this->index
continue
end

# Each small boulder, with the three values the draws gave (after the prologue: the arguments are in place).
break Titania_TiBoulder_Spawn
commands
silent
printf "w7trace boulder-piece ok=1 frame=%d xVel=%f yVel=%f zVel=%f\n", gGameFrameCount, xVel, yVel, zVel
continue
end

run
