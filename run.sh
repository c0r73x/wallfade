#!/bin/sh

if [ ! -f ./build/wallfade ]; then
    make
fi

./build/wallfade -p $HOME/images/wallpapers -i 60 -o ./overlay
