#
# Copyright (c) 2022-present, Trail of Bits, Inc.
# All rights reserved.
#
# This source code is licensed in accordance with the terms specified in
# the LICENSE file found in the root directory of this source tree.
#

SHELL		=	/usr/bin/env bash

CLANG_FLAGS	=	-c -emit-llvm
C_DIFFS		:=	$(patsubst %.c,%.diff,$(wildcard *.c))
CPP_DIFFS	:=	$(patsubst %.cpp,%.diff,$(wildcard *.cpp))
LL_DIFFS	:=	$(patsubst %.ll,%.diff,$(wildcard *.ll))

.PHONY: all clean

%.bc : %.c $(OLD_RELLIC) $(NEW_RELLIC)
	@$(CLANG) $(CLANG_FLAGS) -o $@ $< > /dev/null

%.bc : %.cpp $(OLD_RELLIC) $(NEW_RELLIC)
	@$(CLANG) $(CLANG_FLAGS) -o $@ $< > /dev/null

%.diff : %.bc
	@echo "### $<"
	@echo ""
	@echo "\`\`\`diff"
	-@diff -u \
		<( $(OLD_RELLIC) --input $< --output /dev/stdout ) \
		<( $(NEW_RELLIC) --input $< --output /dev/stdout ) || true
	@echo "\`\`\`"
	@echo ""

%.diff : %.ll
	@echo "### $<"
	@echo ""
	@echo "\`\`\`diff"
	-@diff -u \
		<( $(OLD_RELLIC) --input $< --output /dev/stdout ) \
		<( $(NEW_RELLIC) --input $< --output /dev/stdout ) || true
	@echo "\`\`\`"
	@echo ""

all: $(C_DIFFS) $(CPP_DIFFS) $(LL_DIFFS)

clean:
	-rm *.bc