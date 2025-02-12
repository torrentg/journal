# -Wconversion not set because acutest.h and tests.c warnings pollutes output
CFLAGS= -std=c99 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wpedantic -Wnull-dereference -pthread
LDFLAGS= -lpthread

TARGETS = tests example performance

.PHONY: all clean coverage valgrind helgrind cppcheck loc

all: $(TARGETS)

tests: tests.c logdb.h  logdb.c
	$(CC) -g $(CFLAGS) -DRUNNING_ON_VALGRIND -o $@ tests.c $(LDFLAGS)

example: example.c logdb.h logdb.c
	$(CC) -g $(CFLAGS) -o $@ example.c logdb.c $(LDFLAGS)

performance: performance.c logdb.h logdb.c
	$(CC) -g $(CFLAGS) -o $@ performance.c logdb.c $(LDFLAGS)

coverage: tests.c logdb.h logdb.c
	$(CC) --coverage -O0 $(CFLAGS) -o tests-coverage tests.c -lgcov $(LDFLAGS)
	./tests-coverage
	[ -d coverage ] || mkdir coverage
	lcov --no-external -d . -o coverage/coverage.info -c
	genhtml -o coverage coverage/coverage.info

valgrind: tests
	valgrind --tool=memcheck --leak-check=yes ./tests

helgrind: performance
	valgrind --tool=helgrind --history-backtrace-size=50 ./performance --msw=1 --bpr=10KB --rpc=40 --msr=1 --rpq=100

cppcheck: logdb.h logdb.c
	cppcheck --enable=all  --suppress=missingIncludeSystem --suppress=unusedFunction --suppress=checkersReport logdb.h logdb.c

loc:
	cloc logdb.h logdb.c tests.c example.c performance.c

clean: 
	rm -f $(TARGETS)
	rm -f *.dat *.idx *.tmp *.gcda *.gcno
	rm -f tests-coverage
	rm -rf coverage/
