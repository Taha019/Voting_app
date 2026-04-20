# ── SONU Electronic Voting System — Assignment 3 ─────────────────
# UDP Connectionless Client-Server + fork() per request (Linux/POSIX)
#
# Build:   make
# Run:     Start bin/server first, then bin/client in another terminal
# Clean:   make clean

CC      = gcc
CFLAGS  = -Wall -std=c99 -D_POSIX_C_SOURCE=200809L
# No -lws2_32 — this is POSIX, not Winsock

SRC_DIR = src
HDR_DIR = headers
OBJ_DIR = obj
BIN_DIR = bin

# Server links with all business-logic modules
SERVER_SRCS = $(SRC_DIR)/server.c $(SRC_DIR)/voter.c $(SRC_DIR)/candidate.c \
              $(SRC_DIR)/election.c $(SRC_DIR)/file_io.c
SERVER_OBJS = $(OBJ_DIR)/server.o $(OBJ_DIR)/voter.o $(OBJ_DIR)/candidate.o \
              $(OBJ_DIR)/election.o $(OBJ_DIR)/file_io.o

# Client is self-contained (only needs positions.h at compile time)
CLIENT_SRCS = $(SRC_DIR)/client.c
CLIENT_OBJS = $(OBJ_DIR)/client.o

.PHONY: all clean

all: $(BIN_DIR)/server $(BIN_DIR)/client

$(BIN_DIR)/server: $(SERVER_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(SERVER_OBJS)

$(BIN_DIR)/client: $(CLIENT_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(CLIENT_OBJS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -I$(HDR_DIR) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

clean:
	rm -f $(OBJ_DIR)/*.o $(BIN_DIR)/server $(BIN_DIR)/client
