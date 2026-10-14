CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -g
CPPFLAGS = -D_GNU_SOURCE -Iinclude
TARGET = myls
SOURCES = src/main.c src/options.c src/listing.c src/sort.c src/display.c src/util.c
OBJECTS = $(SOURCES:.c=.o)
.PHONY: all clean test
all: $(TARGET)
$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) -o $@
src/%.o: src/%.c include/myls.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
test: $(TARGET)
	python3 tests/test_myls.py
clean:
	$(RM) $(TARGET) $(OBJECTS)
