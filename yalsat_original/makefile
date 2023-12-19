CC=gcc
CFLAGSO= -Wall -Wextra -static -O3 -funroll-loops -fexpensive-optimizations 
CFLAGSD=  -g -O0  -pg -ggdb -Wall -Wno-missing-braces -static 

all: probSAT #probSATnX

probSAT:	probSAT.c
			$(CC) $(CFLAGSO) probSAT.c -lm -o probSAT

debug:	probSAT.c
			$(CCold) $(CFLAGSD)  probSAT.c -lm -o probSAT
clean:	
		rm -f probSAT


