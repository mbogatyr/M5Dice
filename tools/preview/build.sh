#!/bin/sh
# Builds tools/preview from the firmware's own lib/ sources into /tmp/dice_preview.
cd "$(dirname "$0")/../.." || exit 1
LIBS="DiceMath DiceMesh DiceRaster Randomness RollPlanner DisplayTimeout DiceApp Compose DiceScreen DiceSound"
INCLUDES="-Isrc"
SOURCES="tools/preview/main.cpp src/UiAssets.cpp"
for lib in $LIBS; do
    INCLUDES="$INCLUDES -Ilib/$lib"
    for f in lib/$lib/*.cpp; do
        [ -e "$f" ] && SOURCES="$SOURCES $f"
    done
done
exec c++ -std=gnu++17 -O2 -Wall -Wextra $INCLUDES $SOURCES -lz -lpthread -o /tmp/dice_preview
