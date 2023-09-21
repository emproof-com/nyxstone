CC := clang++
AR := ar

CXX_FLAGS := -g -gdwarf-4 -Wall -Wextra -std=c++17
BUILDDIR := build

NYXSTONE_LLVM_PREFIX ?=  ""

ifeq ($(NYXSTONE_LLVM_PREFIX), "")
LLVM_CONFIG:="llvm-config"
$(info Assuming llvm-config is in path)
else
LLVM_CONFIG:="$(NYXSTONE_LLVM_PREFIX)/bin/llvm-config"
endif

LLVM_MAJOR_VERSION := $(shell $(LLVM_CONFIG) --version | cut -d. -f1)
ifneq ("$(LLVM_MAJOR_VERSION)", "15")
$(error Needs llvm version 15, currently installed is $(LLVM_MAJOR_VERSION))
endif


INCLUDES := -I./src -I./include -I$(shell $(LLVM_CONFIG) --includedir)
VPATH = src/ # src/Target/AArch64/MCTargetDesc/
SOURCES := nyxstone.cpp ObjectWriterWrapper.cpp ELFStreamerWrapper.cpp 
# AArch64MCExpr.cpp
OBJS := $(addprefix $(BUILDDIR)/,$(notdir ${SOURCES:.cpp=.o}))

# LLVM lib directory
LDFLAGS := $(shell $(LLVM_CONFIG) --ldflags)

LLVM_LIBS := $(shell $(LLVM_CONFIG) --system-libs --libs core mc all-targets --link-static 2> /dev/null)
ifneq ($(.SHELLSTATUS), 0)
$(error Cannot link statically, aborting.)
endif

sample: $(BUILDDIR)/nyxstone_sample

run-sample: sample
	@echo "Running sample:"
	@./$(BUILDDIR)/nyxstone_sample

all: static-lib sample

static-lib: $(BUILDDIR)/nyxstone.a

$(BUILDDIR)/nyxstone_sample: examples/sample.cpp $(OBJS)
	@echo "\$$(CC) -o $@ \$$(CXX_FLAGS) \$$(INCLUDES) \$$(LDFLAGS) $^ \$$(LLVM_LIBS)"
	@$(CC) -o $@ $(CXX_FLAGS) $(INCLUDES) $(LDFLAGS) $^ $(LLVM_LIBS)

$(BUILDDIR)/nyxstone.a: $(OBJS)
	@echo "\$$(AR) rcs $@ $^"
	@$(AR) rcs $@ $^

$(BUILDDIR)/%.o: %.cpp | $(BUILDDIR)
	@echo "\$$(CC) -c -o $@ \$$(CXX_FLAGS) \$$(INCLUDES) $<"
	@$(CC) -c -o $@ $(CXX_FLAGS) $(INCLUDES) $<

$(BUILDDIR):
	mkdir $(BUILDDIR)

clean:
	rm -rf $(BUILDDIR)
