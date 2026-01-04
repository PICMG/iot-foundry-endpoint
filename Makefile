.PHONY: all test

all: test

.PHONY: test
test:
	$(MAKE) -C tests run
