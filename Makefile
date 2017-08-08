CC=g++ -std=c++11
FLAG=-D DEBUG


watchdog:watchdog.o
	$(CC) $(FLAG) watchdog.o

watchdog.o:watchdog.cpp cpuuse.h maile.h 
	$(CC) $(FLAG) -c watchdog.cpp -o watchdog.o
clear:
	-rm -f *.o
	-rm -f .so
	-rm -f *.gch
