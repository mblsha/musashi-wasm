# Just a basic makefile to quickly test that everyting is working, it just
# compiles the .o and the generator

# Perfetto support - set ENABLE_PERFETTO=1 to enable
ENABLE_PERFETTO ?= 0

# CC        = gcc
CC        = em++
# CC        = emcc
WARNINGS  = -Wall -Wextra -pedantic
# CFLAGS    = $(WARNINGS) -fsanitize=address -g
# LFLAGS    = $(WARNINGS) -fsanitize=address -g
CFLAGS    = $(WARNINGS) -O3 -frtti -fexceptions -std=c++17
LFLAGS    = $(WARNINGS) -O3 -frtti -fexceptions -std=c++17

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
