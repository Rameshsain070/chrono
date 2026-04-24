CC := gcc
CFLAGS := -Wall -Wextra -std=c99 -pthread
TARGET := chrono
SRC := src/main.c src/mempool.c src/sync.c src/scheduler.c
INCLUDES := -Iinclude

all: $(TARGET)

$(TARGET): $(SRC)
$(CC) $(CFLAGS) $(INCLUDES) -o $(TARGET) $(SRC)

run: $(TARGET)
./$(TARGET)

clean:
rm -f $(TARGET)
