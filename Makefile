CC=gcc
CFLAGS=-g -std=c99 -Wall -Wextra -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE

.PHONY: clean

all: sm smc
sm: sm.o main.o
smc: smc.o
clean:
	rm sm.o main.o smc.o sm smc
