CC=msp430-gcc
LD=msp430-ld
CFLAGS=-O2

all: 	main.c
		$(CC) -mmcu=msp430g2231 -o main main.c -g
		$(CC) -mmcu=msp430g2231 main.c -S
