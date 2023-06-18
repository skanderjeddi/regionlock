all: rl_lock_library

CC = gcc
CCFLAGS = -Wall -o region_lock_library -g -L. -I.

rl_lock_library: region_lock_library.c region_lock_library_test_units.c region_lock_library.h
	$(CC) $(CCFLAGS) region_lock_library.c region_lock_library_test_units.c hues.o -lpthread -pthread

clean:
	rm -f *.o region_lock_library

# Command to run make then valgrind --leak-check=full ./rl_lock_library
# Path: Makefile
memcheck: region_lock_library_test_units
	valgrind --leak-check=full ./region_lock_library_test_units

