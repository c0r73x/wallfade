# wallfade
Application to get fading wallpapers in X

```
Usage: wallfade [options]
    -f, fade    : fade time (default 1.0s)
    -i, idle    : idle time (default 3s)
    -s, smooth  : smoothing function to use when fading.
                    1: linear
                    2: smoothstep (default)
                    3: smootherstep

    -p, paths   : wallpapers paths.
                    use monitor:path, if no monitor is
                    specified the path will be used as
                    the default path

                    example: -p 0:path,1:path,path

    -l, lower   : finds and lowers window by classname (e.g. Conky)
    -c, center  : center wallpapers
    -m, message : send message to running process (-m help)
    -h, help    : help
```
