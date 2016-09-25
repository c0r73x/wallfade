CC=gcc
CWAND=$(shell Wand-config --cflags)
LDWAND=$(shell Wand-config --ldflags)
CFLAGS=-O3 -fopenmp -std=gnu11 -lGL -lX11 -lbsd -lXrandr $(CWAND) $(LDWAND)

all:
	$(CC) -o wallfade wallfade.c $(CFLAGS)

clean:
	@rm wallfade
