CFLAGS= -std=gnu11 -Wall -Wextra -Wpedantic -Wnull-dereference

# -Wconversion

all: tests coverage cppcheck valgrind

tests: logdb.h tests.c
	$(CC) -g $(CFLAGS) -o tests tests.c
	./tests

coverage: logdb.h tests.c
	$(CC) --coverage -O0 $(CFLAGS) -o tests-coverage tests.c -lgcov
	./tests-coverage
	[ -d coverage ] || mkdir coverage
	lcov --no-external -d . -o coverage/coverage.info -c
	genhtml -o coverage coverage/coverage.info

cppcheck: logdb.h
	cppcheck --enable=all  --suppress=missingIncludeSystem --suppress=unusedFunction --suppress=checkersReport logdb.h

valgrind: logdb.h tests.c
	$(CC) -g $(CFLAGS) -DRUNNING_ON_VALGRIND -o tests-valgrind tests.c
	valgrind --tool=memcheck --leak-check=yes ./tests-valgrind

clean: 
	rm -f tests test.dat test.idx
	rm -f tests-coverage
	rm -f tests-valgrind
	rm -f *.gcda *.gcno
	rm -rf coverage
