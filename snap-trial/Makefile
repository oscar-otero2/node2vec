#
# Makefile for non-Microsoft compilers
#

all: MakeAll

test: TestAll

MakeAll:
	$(MAKE) -C examples
# $(MAKE) -C test

TestAll:
	$(MAKE) run -C test

clean:
	$(MAKE) clean -C snap-core
	$(MAKE) clean -C examples
	$(MAKE) clean -C test
