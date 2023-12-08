CC = gcc
CFLAGS = -Wall

SRC_DIR = src
EXEC = download

$(EXEC): $(SRC_DIR)/download.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(EXEC)
