CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -std=c11 -O0 -g
CPPFLAGS += -Iinclude -D_DEFAULT_SOURCE

BUILD := build
LIB   := $(BUILD)/libuthreads.a
DEMO  := $(BUILD)/uthreads_demo
OBJS  := $(BUILD)/uthreads.o $(BUILD)/context.o

.PHONY: all run clean

all: $(DEMO)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c $(wildcard include/*.h) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(LIB): $(OBJS)
	ar rcs $@ $^

$(DEMO): examples/demo.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -luthreads -o $@

run: $(DEMO)
	./$(DEMO)

clean:
	rm -rf $(BUILD)
