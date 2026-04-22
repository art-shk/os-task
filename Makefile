CC = gcc
CFLAGS_LIB = -Wall -Wextra -pedantic -fPIC -shared
CFLAGS_APP = -Wall -Wextra -pthread
CFLAGS_TEST = -Wall -Wextra

LIB = libcaesar.so
TEST_PROG = test_prog
SECURE_COPY = secure_copy

# Директория для тестов
TEST_DIR = test
INPUT_DIR = ${TEST_DIR}/input
OUTPUT_DIR = ${TEST_DIR}/output

all: $(LIB)

$(LIB): libcaesar.c libcaesar.h
	$(CC) $(CFLAGS_LIB) libcaesar.c -o $(LIB)

install: $(LIB)
	cp $(LIB) /usr/local/lib/
	-@ldconfig 2>/dev/null || true

$(TEST_PROG): test.c libcaesar.h
	$(CC) $(CFLAGS_TEST) test.c -o $(TEST_PROG) -ldl

# Сборка secure_copy с поддержкой libcaesar.so (через dlopen)
$(SECURE_COPY): secure_copy.c libcaesar.h
	$(CC) $(CFLAGS_APP) secure_copy.c -o $(SECURE_COPY) -lpthread -ldl

# Тест задания 1
test: $(LIB) $(TEST_PROG)
	./$(TEST_PROG) ./$(LIB) 'X' input.txt output.txt

# Тест задания 4
test4: $(LIB) $(SECURE_COPY)
	./$(SECURE_COPY) \
		${INPUT_DIR}/f1.txt \
		${INPUT_DIR}/f2.txt \
		${INPUT_DIR}/f3.txt \
		${INPUT_DIR}/f4.txt \
		${INPUT_DIR}/f5.txt \
		${OUTPUT_DIR} 65

clean:
	rm -f $(LIB) $(TEST_PROG) $(SECURE_COPY) log.txt
	rm -rf $(TEST_DIR)

.PHONY: all install test test4 clean