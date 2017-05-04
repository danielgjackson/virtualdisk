BIN_NAME = virtualdisk-test
CC = gcc
CFLAGS = -O2 -Wall -march=native
LIBS = -lm

SRC = $(wildcard virtualdisk/*.c) $(wildcard test/*.c) $(wildcard fatfs/*.c)
INC = $(wildcard virtualdisk/*.h) $(wildcard test/*.h) $(wildcard fatfs/*.h)
INC_DIR = -I. -Ivirtualdisk -Itest

all: $(BIN_NAME)

$(BIN_NAME): Makefile $(SRC) $(INC)
	$(CC) -std=c99 -o $(BIN_NAME) $(CFLAGS) $(SRC) $(INC_DIR) -I/usr/local/include -L/usr/local/lib $(LIBS)

clean:
	rm -f *.o core $(BIN_NAME)
