CC = gcc
CFLAGS_LIB = -Wall -Wextra -pedantic -fPIC -shared
CFLAGS_APP = -Wall -Wextra -pthread
CFLAGS_TEST = -Wall -Wextra

LIB = libcaesar.so
TEST_PROG = test_prog
SECURE_COPY = secure_copy

all: $(LIB)

$(LIB): libcaesar.c libcaesar.h
	$(CC) $(CFLAGS_LIB) libcaesar.c -o $(LIB)

install: $(LIB)
	cp $(LIB) /usr/local/lib/
	-@ldconfig 2>/dev/null || true

$(TEST_PROG): test.c libcaesar.h
	$(CC) $(CFLAGS_TEST) test.c -o $(TEST_PROG) -ldl

$(SECURE_COPY): secure_copy.c libcaesar.c libcaesar.h
	$(CC) $(CFLAGS_APP) secure_copy.c libcaesar.c -o $(SECURE_COPY) -lpthread

# Тест задания 1
test: $(LIB) $(TEST_PROG)
	./$(TEST_PROG) ./$(LIB) 'X' input.txt output.txt

# Тест задания 2
test2: $(SECURE_COPY)
	./$(SECURE_COPY) input2.txt output2.txt 65

clean:
	rm -f $(LIB) $(TEST_PROG) $(SECURE_COPY) output.txt output2.txt

.PHONY: all install test test2 clean