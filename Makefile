.PHONY: all
all:
	gcc -O2 -Wno-int-conversion -Wno-error -o code main.c buddy.c
