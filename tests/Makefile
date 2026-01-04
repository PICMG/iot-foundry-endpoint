CC = gcc
CFLAGS = -Wall -Wextra -g -DUNIT_TEST -I../include -I./vendor -I. -DMCTP_EVENT_TX_ENABLED=1
GCOVFLAGS = -fprofile-arcs -ftest-coverage

SRCS = ../src/mctp.c ../src/fcs.c platform_mock.c test_mctp.c
OBJS = $(SRCS:.c=.o)

.PHONY: all clean run coverage
all: test_mctp

test_mctp: $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(SRCS)

run: test_mctp
	./test_mctp

coverage: CFLAGS += $(GCOVFLAGS)
coverage: LDFLAGS += $(GCOVFLAGS)
coverage: clean
	$(CC) $(CFLAGS) $(LDFLAGS) -o test_mctp_coverage $(SRCS)
	./test_mctp_coverage
	@echo "Generating coverage report..."
	@if which gcovr >/dev/null 2>&1; then \
		gcovr -r .. --html-details -o coverage/tests-coverage.html || true; \
		echo "Coverage report: coverage/tests-coverage.html"; \
	else \
		echo "gcovr not found; falling back to gcov per-source (results in stdout)."; \
		gcov -b -c -o tests ../src/mctp.c || true; \
		gcov -b -c -o tests ../src/fcs.c || true; \
	fi

clean:
	rm -f test_mctp test_mctp_coverage *.o *.gcno *.gcda *.gcov
