CC		:= gcc
CFLAGS 	:= -Wall -Wextra -O2 -g
BIN		:= bin
SRC		:= src
INCLUDE	:= include
LIB		:= lib
OBJ		:= obj

LIBRARIES	:=

RM	:= rm -rf

ifeq ($(OS),Windows_NT)
EXECUTABLE	:= main.exe
else
EXECUTABLE	:= main
endif

SRCS	:= $(notdir $(wildcard $(SRC)/*.c))
OBJS	:= $(patsubst %.c, $(OBJ)/%.o, $(SRCS))

$(OBJ)/%.o: $(SRC)/%.c
	mkdir -p $(OBJ)
	$(CC) $(CFLAGS) -I$(INCLUDE) -c $^ -o $@

all: $(BIN)/$(EXECUTABLE)

clean:
	-$(RM) $(BIN)/$(EXECUTABLE) $(OBJS)

run: all
	./$(BIN)/$(EXECUTABLE)

$(BIN)/$(EXECUTABLE): $(OBJS)
	mkdir -p $(BIN)
	$(CC) $(CFLAGS) -I$(INCLUDE) -L$(LIB) $^ -o $@ $(LIBRARIES)