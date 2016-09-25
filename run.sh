#!/bin/sh

if [ ! -f wallfade ]; then
    make
fi

xwinwrap -b -fs -sp -fs -nf -ov -- ./wallfade -w WID -p $HOME/images/wallpapers
