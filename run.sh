#!/bin/sh

if [ ! -f ./build/wallfade ]; then
    make
fi

./build/wallfade -x ./wallfade.ini.sample -p 1:$HOME/images/wallpapers/**/ $@
