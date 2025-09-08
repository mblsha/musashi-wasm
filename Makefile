# Just a basic makefile to quickly test that everyting is working, it just
# compiles the .o and the generator

# Perfetto support - set ENABLE_PERFETTO=1 to enable
ENABLE_PERFETTO ?= 0

# CC        = gcc
CC        = em++
# CC        = emcc
WARNINGS  = -Wall -Wextra -pedantic

# Configurable build flags
EXCEPTIONS ?= 1
MUSASHI_NO_SETJMP ?= 0
M68K_TRACE_RAM_FLOW_LOG ?= 0

# Base flags
CXXSTD    = -std=c++17
OPTFLAGS  = -O3 -frtti
ifeq ($(EXCEPTIONS),1)
  EXCFLAGS = -fexceptions
else
  EXCFLAGS = -fno-exceptions
endif

CDEFS    :=
ifeq ($(MUSASHI_NO_SETJMP),1)
  CDEFS += -DMUSASHI_NO_SETJMP=1
endif
ifeq ($(M68K_TRACE_RAM_FLOW_LOG),1)
  CDEFS += -DM68K_TRACE_RAM_FLOW_LOG=1
endif

# CFLAGS    = $(WARNINGS) -fsanitize=address -g
# LFLAGS    = $(WARNINGS) -fsanitize=address -g
CFLAGS    = $(WARNINGS) $(OPTFLAGS) $(EXCFLAGS) $(CXXSTD) $(CDEFS)
LFLAGS    = $(WARNINGS) $(OPTFLAGS) $(EXCFLAGS) $(CXXSTD) $(CDEFS)

MUSASHIFILES     = m68kcpu.c myfunc.cc m68k_memory_bridge.cc m68kdasm.c m68ktrace.cc softfloat/softfloat.c

# Add Perfetto files if enabled
ifeq ($(ENABLE_PERFETTO),1)
    MUSASHIFILES += m68k_perfetto.cc third_party/retrobus-perfetto/cpp/proto/perfetto.pb.cc
    PERFETTO_FLAGS = -DENABLE_PERFETTO=1 -Ithird_party/retrobus-perfetto/cpp/include -Ithird_party/retrobus-perfetto/cpp/proto -Ithird_party/protobuf-wasm-install/include
    CFLAGS += $(PERFETTO_FLAGS)
    LFLAGS += -DENABLE_PERFETTO=1
    # Note: For full Perfetto support in Makefile builds, protobuf libs would be needed
    # This is primarily for WASM builds where dependencies are handled differently
endif
MUSASHIGENCFILES = m68kops.c
MUSASHIGENHFILES = m68kops.h
MUSASHIGENERATOR = m68kmake

EXE =
EXEPATH = ./

.CFILES   = $(filter %.c,$(MUSASHIFILES)) $(MUSASHIGENCFILES)
.CCFILES  = $(filter %.cc,$(MUSASHIFILES))
.OFILES   = $(.CFILES:%.c=%.o) $(.CCFILES:%.cc=%.o)

# Rule for compiling .cc files with the same compiler
%.o: %.cc
	$(CC) $(CFLAGS) -c -o $@ $<

DELETEFILES = $(MUSASHIGENCFILES) $(MUSASHIGENHFILES) $(.OFILES) $(TARGET) $(MUSASHIGENERATOR)$(EXE)


all: $(.OFILES)

clean:
	rm -f $(DELETEFILES)

m68kcpu.o: $(MUSASHIGENHFILES) m68kfpu.c m68kmmu.h softfloat/softfloat.c softfloat/softfloat.h

$(MUSASHIGENCFILES) $(MUSASHIGENHFILES): $(MUSASHIGENERATOR)$(EXE)
	$(EXEPATH)$(MUSASHIGENERATOR)$(EXE)

$(MUSASHIGENERATOR)$(EXE):  $(MUSASHIGENERATOR).c
	gcc -o  $(MUSASHIGENERATOR)$(EXE)  $(MUSASHIGENERATOR).c
