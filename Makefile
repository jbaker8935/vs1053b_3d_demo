# Makefile for arcade
# Build artifacts go into `build/`

ROOT := $(CURDIR)
BUILD_DIR := build
NAME = vs1053b_3d_demo

FOENIX_MGR := "/home/john/code/FoenixMgr/fm.sh"

# Tool locations (use the user's tool locations by default)
CC := /opt/llvm-mos/bin/mos-f256-clang
LD := /opt/llvm-mos/bin/ld.lld
MOS_LIB_DIR ?= /opt/llvm-mos/mos-platform/f256/lib
OVERLAY := /opt/llvm-mos/bin/overlay
PGZ_THUNK := /opt/llvm-mos/bin/pgz-thunk.py

# Common tool helpers (override if needed)
NM ?= llvm-nm
OBJDUMP ?= llvm-objdump
OBJCOPY ?= llvm-objcopy
PYTHON ?= python3

LDSCRIPT ?= f256.ld
CFLAGS ?= -I$(ROOT) -I$(ROOT)/src -I$(ROOT)/include -I/opt/llvm-mos/include -Os -Wall -D__llvm_mos__
ASMFLAGS ?= -I$(ROOT) -I$(ROOT)/src -I$(ROOT)/include -I/opt/llvm-mos/include -Wall
LDFLAGS ?=

# Collect sources from src/ only. Explicitly exclude libfixmath and other dirs.
SRCS_C := $(wildcard src/*.c)
SRCS_S := $(wildcard src/*.s)

SRC_ORDERED := $(wildcard src/*.c src/*.s)
OBJS := $(patsubst src/%.c,build/%.o,$(filter %.c,$(SRC_ORDERED))) \
	$(patsubst src/%.s,build/%.o,$(filter %.s,$(SRC_ORDERED)))

.PHONY: all clean pgz dist run info dirs overlay
all: pgz

# Ensure build dir exists
dirs:
	@mkdir -p $(BUILD_DIR)

COMPILE = $(CC) -c $(CFLAGS) -o $@ $<

COMPILE_ASM = $(CC) -c $(ASMFLAGS) -o $@ $<

# Host C compiler for build-time tools (used to compile the offsets generator)
CC_HOST ?= cc

# Build an offsets generator binary using the host compiler
tools/gen_offsets: tools/generate_offsets.c | dirs
	$(CC_HOST) -Iinclude -DGENERATE_OFFSETS $< -o $@

# Target-derived struct offsets (authoritative): compile with llvm-mos and extract `.equ` lines.
build/struct_offsets_emit.s: tools/struct_offsets_emit.c include/draw_line.h include/3d_object.h include/3d_math.h | dirs
	$(CC) -S $(CFLAGS) -o $@ $<

# Generated assembler offsets (single shared file)
build/struct_offsets.inc: build/struct_offsets_emit.s | dirs
	$(PYTHON) tools/extract_struct_offsets_equ.py $< > $@

# Compile both C and assembly with the same command (mos-f256-clang)
$(BUILD_DIR)/%.o: src/%.c | dirs
	$(COMPILE)

$(BUILD_DIR)/%.o: src/%.s build/struct_offsets.inc | dirs
	$(COMPILE_ASM)

# Compile the bench harness (not in src/) so it can be built independently
$(BUILD_DIR)/bench_mul.o: tools/bench_mul.c | dirs
	$(CC) -c $(CFLAGS) -o $@ $<

# Link (produces an output binary named '$(NAME)')
$(BUILD_DIR)/$(NAME): $(OBJS) | dirs
	@mkdir -p $(BUILD_DIR)
	(cd $(BUILD_DIR) && \
	$(CC) \
		-D__llvm_mos__ \
		-T ../$(LDSCRIPT) \
		-Wl,-Map=$(NAME).map \
		-o $(NAME) \
		-I.. \
		-Os -Wall -lm \
		$(notdir $(OBJS)) \
		$(LDFLAGS))

# Prepare overlayed build directory from project sources.
overlay: dirs
	@if command -v $(OVERLAY) >/dev/null 2>&1; then \
		$(OVERLAY) 5 $(BUILD_DIR) $(ROOT)/src || { echo "overlay failed" >&2; exit 1; }; \
	else \
		echo "overlay not found; skipping overlay step"; \
	fi
	@# Always ensure assets required by `.incbin` are present in build/assets.
	@mkdir -p $(BUILD_DIR)/assets
	@cp -a $(ROOT)/src/assets/* $(BUILD_DIR)/assets/ 2>/dev/null || true
	@cp -a $(ROOT)/assets/* $(BUILD_DIR)/assets/ 2>/dev/null || true

# Ensure overlay runs before linking so linker sees overlayed files
$(BUILD_DIR)/$(NAME): overlay $(OBJS) | dirs


# Post-link: symbol and listing, then overlay/pgz creation
.SILENT: $(BUILD_DIR)/$(NAME).pgz
$(BUILD_DIR)/$(NAME).pgz: $(BUILD_DIR)/$(NAME)
	@# If linker produced an ELF variant, use it for symbol/listing; otherwise skip.
	@if [ -f $(BUILD_DIR)/$(NAME).elf ]; then \
		ELF_FILE=$(BUILD_DIR)/$(NAME).elf; \
	elif [ -f $(BUILD_DIR)/$(NAME).elf.elf ]; then \
		ELF_FILE=$(BUILD_DIR)/$(NAME).elf.elf; \
	else \
		ELF_FILE=; \
	fi; \
	if [ -n "$$ELF_FILE" ]; then \
		$(NM) "$$ELF_FILE" > $(BUILD_DIR)/$(NAME).sym || true; \
		$(OBJDUMP) --syms -d --print-imm-hex "$$ELF_FILE" > $(BUILD_DIR)/$(NAME).lst || true; \
	fi

	# If linker produced a raw binary named '$(NAME)', promote it to .pgz as f256build.sh did
	if [ -f $(BUILD_DIR)/$(NAME) ]; then \
		mv $(BUILD_DIR)/$(NAME) $(BUILD_DIR)/$(NAME).pgz; \
	fi

	# If a .pgz was created by overlay or the linker, run pgz-thunk to inspect it
	if [ -f $(BUILD_DIR)/$(NAME).pgz ]; then \
		SCRIPT=""; \
		if [ -f "$(PGZ_THUNK)" ]; then \
			SCRIPT="$(PGZ_THUNK)"; \
		elif command -v $(PGZ_THUNK) >/dev/null 2>&1; then \
			SCRIPT="$$(command -v $(PGZ_THUNK))"; \
		fi; \
		if [ -n "$$SCRIPT" ]; then \
			$(PYTHON) "$$SCRIPT" $(BUILD_DIR)/$(NAME).pgz || echo "pgz-thunk did not process $(BUILD_DIR)/$(NAME).pgz"; \
		fi; \
	fi

	# Ensure a PGZ artifact was produced by overlay/pgz-thunk.
	if [ ! -f $(BUILD_DIR)/$(NAME).pgz ]; then \
		echo "ERROR: pgz-thunk did not produce $(BUILD_DIR)/$(NAME).pgz" >&2; \
		exit 1; \
	fi

	cp $(BUILD_DIR)/$(NAME).pgz bin/

pgz: $(BUILD_DIR)/$(NAME).pgz

dist: pgz
	@echo "dist produced: bin/$(NAME).pgz"


copy: pgz
	@echo "Transferring $(ROOT)/bin/$(NAME).pgz to Foenix..."
	@$(FOENIX_MGR) --port /dev/ttyUSB2 --copy $(ROOT)/bin/$(NAME).pgz

dump: 
	@echo "Reading memory at offset 0x0000"
	@$(FOENIX_MGR) --port /dev/ttyUSB2 --dump 0
	
run: pgz
	@echo "Running $(NAME) on Foenix..."
	@$(FOENIX_MGR) --port /dev/ttyUSB2 --run-pgz $(ROOT)/bin/$(NAME).pgz

# Benchmarks
BENCH_NAME := bench_mul
BENCH_OBJ := $(BUILD_DIR)/$(BENCH_NAME).o

bench: $(BUILD_DIR)/$(BENCH_NAME).pgz
	@echo "bench build complete: $<"

$(BUILD_DIR)/$(BENCH_NAME): $(BENCH_OBJ) | dirs
	$(CC) -D__llvm_mos__ -T $(LDSCRIPT) -o $@ $(BENCH_OBJ) -I.. -Os -Wall -lm

$(BUILD_DIR)/$(BENCH_NAME).pgz: $(BUILD_DIR)/$(BENCH_NAME)
	@# Wrap bench binary in .pgz format (same as normal build)
	@if [ -f $(BUILD_DIR)/$(BENCH_NAME) ]; then \
		mv $(BUILD_DIR)/$(BENCH_NAME) $(BUILD_DIR)/$(BENCH_NAME).pgz; \
	fi
	@# Copy bench artifact to bin/ for easy access
	@mkdir -p bin
	@cp $(BUILD_DIR)/$(BENCH_NAME).pgz bin/

# T0 pending-flag reliability test (hardware bug investigation)
TEST_T0_PEND_NAME := test_t0_pend
TEST_T0_PEND_OBJ  := $(BUILD_DIR)/$(TEST_T0_PEND_NAME).o

test_t0_pend: $(BUILD_DIR)/$(TEST_T0_PEND_NAME).pgz
	@echo "test_t0_pend build complete: $<"

$(BUILD_DIR)/$(TEST_T0_PEND_NAME).o: tools/$(TEST_T0_PEND_NAME).c | dirs
	$(CC) -c $(CFLAGS) -o $@ $<

$(BUILD_DIR)/$(TEST_T0_PEND_NAME): $(BUILD_DIR)/$(TEST_T0_PEND_NAME).o | dirs
	$(CC) -D__llvm_mos__ -T $(LDSCRIPT) -o $@ $< -I.. -Os -Wall -lm

$(BUILD_DIR)/$(TEST_T0_PEND_NAME).pgz: $(BUILD_DIR)/$(TEST_T0_PEND_NAME)
	@if [ -f $(BUILD_DIR)/$(TEST_T0_PEND_NAME) ]; then \
		mv $(BUILD_DIR)/$(TEST_T0_PEND_NAME) $(BUILD_DIR)/$(TEST_T0_PEND_NAME).pgz; \
	fi
	@mkdir -p bin
	@cp $(BUILD_DIR)/$(TEST_T0_PEND_NAME).pgz bin/

# far_mvn stress test
TEST_FAR_MVN_NAME := test_far_mvn
TEST_FAR_MVN_OBJ  := $(BUILD_DIR)/$(TEST_FAR_MVN_NAME).o

test_far_mvn: $(BUILD_DIR)/$(TEST_FAR_MVN_NAME).pgz
	@echo "test_far_mvn build complete: $<"

$(BUILD_DIR)/$(TEST_FAR_MVN_NAME).o: tools/$(TEST_FAR_MVN_NAME).c | dirs
	$(CC) -c $(CFLAGS) -o $@ $<

$(BUILD_DIR)/$(TEST_FAR_MVN_NAME): $(BUILD_DIR)/$(TEST_FAR_MVN_NAME).o | dirs
	$(CC) -D__llvm_mos__ -T $(LDSCRIPT) -o $@ $< -I.. -Os -Wall -lm

$(BUILD_DIR)/$(TEST_FAR_MVN_NAME).pgz: $(BUILD_DIR)/$(TEST_FAR_MVN_NAME)
	@if [ -f $(BUILD_DIR)/$(TEST_FAR_MVN_NAME) ]; then \
		mv $(BUILD_DIR)/$(TEST_FAR_MVN_NAME) $(BUILD_DIR)/$(TEST_FAR_MVN_NAME).pgz; \
	fi
	@# Generate listing if an ELF variant exists (same behavior as main target)
	@if [ -f $(BUILD_DIR)/$(TEST_FAR_MVN_NAME).elf ]; then \
		ELF_FILE=$(BUILD_DIR)/$(TEST_FAR_MVN_NAME).elf; \
	elif [ -f $(BUILD_DIR)/$(TEST_FAR_MVN_NAME).elf.elf ]; then \
		ELF_FILE=$(BUILD_DIR)/$(TEST_FAR_MVN_NAME).elf.elf; \
	else \
		ELF_FILE=; \
	fi; \
	if [ -n "$${ELF_FILE}" ]; then \
		$(NM) "$${ELF_FILE}" > $(BUILD_DIR)/$(TEST_FAR_MVN_NAME).sym || true; \
		$(OBJDUMP) --syms -d --print-imm-hex "$${ELF_FILE}" > $(BUILD_DIR)/$(TEST_FAR_MVN_NAME).lst || true; \
	fi
	@mkdir -p bin
	@cp $(BUILD_DIR)/$(TEST_FAR_MVN_NAME).pgz bin/

info:
	@echo "ROOT = $(ROOT)"
	@echo "BUILD_DIR = $(BUILD_DIR)"
	@echo "NAME = $(NAME)"
	@echo "CC = $(CC)"
	@echo "LD = $(LD)"
	@echo "OVERLAY = $(OVERLAY)"
	@echo "PGZ_THUNK = $(PGZ_THUNK)"
	@echo "LDSCRIPT = $(LDSCRIPT)"
	@echo "CFLAGS = $(CFLAGS)"
	@echo "LDFLAGS = $(LDFLAGS)"
	@echo "SRCS_C = $(SRCS_C)"
	@echo "SRCS_S = $(SRCS_S)"

clean:
	# Remove all files inside the build directory and the generated pgz in bin.
	# Use patterns that avoid removing '.' or '..'
	rm -rf $(BUILD_DIR)/* $(BUILD_DIR)/.[!.]* $(BUILD_DIR)/..?* || true
	rm -f bin/$(NAME).pgz || true

