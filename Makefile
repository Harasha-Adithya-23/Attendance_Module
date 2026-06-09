# Makefile — A600 Attendance HTTP Server

CC      ?= arm-ostl-linux-gnueabi-gcc
CFLAGS  = -O2 -Wall -Wextra -Wno-unused-parameter
LDFLAGS = -lm

TARGET  = attendance_server
SRC     = server.c Attendance.c A600.c
OBJ     = $(SRC:.c=.o)

.PHONY: all clean

all: $(TARGET)

# Link step
$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Built $(TARGET) — copy index.html and app.js to the same directory"

# Compile step
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJ)
