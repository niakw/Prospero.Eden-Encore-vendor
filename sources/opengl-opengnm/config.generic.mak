DESTDIR=/usr/local
BINDIR=/bin
INCDIR=/include
LIBDIR=/lib

LIBEXT=.so
LIBVER=0.1.0

PLATFORM=generic

AR=ar
CC=clang
LD=clang
CFLAGS=-std=c11 -Wall -Wextra -Wpedantic -I./include -I./src -O2 -g
LDFLAGS=-L. -lopengnm -lm
LIB_LDFLAGS=-shared -L. -lm
