# tests/Makefile.in
#
# Provides a 'make check' target compatible with
# the top-level Automake-generated makefile.
#
# Copyright (c) 2013, Matthew Madison.
# All rights reserved.
# Distributed under license.  See LICENSE.TXT for details.

BLISSC = ../build/blissc
TESTDIR = .
PYTHON = python3

.PHONY: check all install distdir dist

check: testharness.lib
	$(PYTHON) "$(TESTDIR)/runtests.py" --blissc="$(BLISSC)" --cc="$(CC)" "$(TESTDIR)"

testharness.lib: $(TESTDIR)/testharness.req
	$(BLISSC) --library -o $@ $<

clean:
	@files=`find $(TESTDIR) -name '*.bli' -print`; \
	 for srcf in $$files ; do f=`basename $$srcf .bli`; \
	 echo "rm -f $$f $$f.o" ; rm -f $$f $$f.o ; done

all install distdir dist:
	@ :
