# ── SONU Voting System — Client only (Windows, MinGW/MSYS2) ──────────
#
# Files needed in this directory:
#   client.c      — client source
#   positions.h   — position constants (included by client.c)
#   Makefile      — this file
#
# Prerequisites:
#   Install MinGW-w64 or MSYS2.  Make sure gcc.exe is on your PATH.
#   Verify with:  gcc --version
#
# Build:
#   make              → produces client.exe
#
# Run:
#   client.exe                   (connects to 127.0.0.1 — same machine)
#   client.exe 192.168.1.42      (server on same LAN)
#   client.exe 203.0.113.5       (server on another network)
#
# Clean:
#   make clean

CC     = gcc
CFLAGS = -Wall -std=c99
LIBS   = -lws2_32

all: client.exe

client.exe: client.c positions.h
	$(CC) $(CFLAGS) -o client.exe client.c $(LIBS)

clean:
	del /f client.exe 2>nul & exit 0
