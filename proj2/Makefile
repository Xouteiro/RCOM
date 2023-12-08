CC = gcc
CFLAGS = -Wall

SRC_DIR = src
EXEC = download

$(EXEC): $(SRC_DIR)/download.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	find . -maxdepth 1 -type f -not -name 'Makefile' -exec rm -f {} \;
