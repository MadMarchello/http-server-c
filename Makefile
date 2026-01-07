CC=gcc
CFLAGS=-Wall -Wextra -pedantic -std=c11
TARGET=server

all: $(TARGET)

$(TARGET): server.c
	$(CC) $(CFLAGS) $< -o $@

test: $(TARGET)
	bash tests/server_tests.sh

clean:
	rm -f $(TARGET)

