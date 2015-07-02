all:parsing.c
	cc -Wall -std=c99 mpc.c parsing.c -ledit -lm -o parsing.out

clean:
	rm -f parsing.out