#!/bin/sh

if [ ! -f wallfade ]; then
    make
fi

./build/wallfade -p $HOME/images/wallpapers
