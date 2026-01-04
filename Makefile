CC=gcc
CFLAGS=-Wall -Wextra -pedantic -std=c11
TARGET=server

all: $(TARGET)

$(TARGET): server.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(TARGET)

