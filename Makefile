CC = gcc
CFLAGS_LIB = -Wall -Wextra -pedantic -fPIC -shared
CFLAGS_TEST = -Wall -Wextra

LIB = libcaesar.so
TEST_PROG = test_prog

all: $(LIB)

$(LIB): libcaesar.c libcaesar.h
	$(CC) $(CFLAGS_LIB) libcaesar.c -o $(LIB)

install: $(LIB)
	cp $(LIB) /usr/local/lib/
	-@ldconfig 2>/dev/null || true

$(TEST_PROG): test.c libcaesar.h
	$(CC) $(CFLAGS_TEST) test.c -o $(TEST_PROG) -ldl

test: $(LIB) $(TEST_PROG)
	./$(TEST_PROG) ./$(LIB) 'X' input.txt output.txt

clean:
	rm -f $(LIB) $(TEST_PROG) output.txt

.PHONY: all install test clean