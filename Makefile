# Compiler and flags
CC = gcc
CFLAGS = -pthread -Wall -Wextra -Iinclude

# Directories
SRC_DIR = src
INC_DIR = include
BUILD_DIR = build

# Files
SOURCES = $(wildcard $(SRC_DIR)/*.c)
HEADERS = $(wildcard $(INC_DIR)/*.h)
OBJECTS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SOURCES))
TARGET = $(BUILD_DIR)/server

# Default target
all: $(TARGET)
	@echo "Build successful! Executing the server..."
	@./$(TARGET)

# Link objects into the final executable
$(TARGET): $(OBJECTS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(OBJECTS)

# Compile source files into object files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(HEADERS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Clean build files
clean:
	rm -rf $(BUILD_DIR)
	@echo "Cleaned build files."
