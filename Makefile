CC = clang
CFLAGS = -Wall -Wextra -Wpedantic -std=c23 -g

TARGET = CEPBEP
SRC = src/main.c src/http.c
OBJ = $(SRC:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all clean
