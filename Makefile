# Makefile for vs1053b_3d_demo
# Build artifacts go into `build/`

ROOT := $(CURDIR)
BUILD_DIR := build
NAME = vs1053b_3d_demo

FOENIX_MGR := "/home/john/code/FoenixMgr/fm.sh"

# Tool locations (use the user's tool locations by default)
# note: f256lib.h was manually copied to /opt/llvm-mos/include
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


# Single object demo
SINGLE_NAME := single_object
SINGLE_SRC := examples/single_object.c
SINGLE_OBJ := $(BUILD_DIR)/$(SINGLE_NAME).o
SINGLE_ASM := $(BUILD_DIR)/$(SINGLE_NAME).s
SINGLE_DEPS := $(BUILD_DIR)/3d_object.o $(BUILD_DIR)/draw_line.o $(BUILD_DIR)/draw_lines_asm.o $(BUILD_DIR)/emit_edges_asm.o $(BUILD_DIR)/geometry_kernel.o $(BUILD_DIR)/video.o $(BUILD_DIR)/vs1053b.o
SINGLE_OBJS := $(SINGLE_OBJ) $(SINGLE_DEPS)
# Separate overlay output dir for single_object (only plugin_data, no other assets)
SINGLE_OVERLAY_DIR := $(BUILD_DIR)/so
SINGLE_SRC_STAGING := $(BUILD_DIR)/so_src

.PHONY: single_object single_object_overlay
single_object: $(BUILD_DIR)/$(SINGLE_NAME).pgz
	@echo "single_object build complete: $<"

# Overlay scoped to only vs1053b.c — produces output.ld with just plugin_data segment
single_object_overlay: dirs
	@mkdir -p $(SINGLE_SRC_STAGING)/assets $(SINGLE_OVERLAY_DIR)/assets
	@cp $(ROOT)/src/vs1053b.c $(SINGLE_SRC_STAGING)/
	@cp -a $(ROOT)/src/assets/* $(SINGLE_SRC_STAGING)/assets/ 2>/dev/null || true
	@cp -a $(ROOT)/assets/* $(SINGLE_SRC_STAGING)/assets/ 2>/dev/null || true
	@cp -a $(ROOT)/src/assets/* $(SINGLE_OVERLAY_DIR)/assets/ 2>/dev/null || true
	@cp -a $(ROOT)/assets/* $(SINGLE_OVERLAY_DIR)/assets/ 2>/dev/null || true
	@if command -v $(OVERLAY) >/dev/null 2>&1; then \
		$(OVERLAY) 5 $(SINGLE_OVERLAY_DIR) $(SINGLE_SRC_STAGING) || { echo "single_object overlay failed" >&2; exit 1; }; \
	else \
		echo "overlay not found; skipping single_object overlay step"; \
	fi

$(BUILD_DIR)/$(SINGLE_NAME).o: $(SINGLE_SRC) | dirs
	$(CC) -c $(CFLAGS) -o $@ $<

$(BUILD_DIR)/$(SINGLE_NAME).s: $(SINGLE_SRC) | dirs
	$(CC) -S $(CFLAGS) -o $@ $<

$(BUILD_DIR)/$(SINGLE_NAME): single_object_overlay $(SINGLE_OBJS) | dirs
	(cd $(SINGLE_OVERLAY_DIR) && \
	$(CC) \
		-D__llvm_mos__ \
		-T ../../$(LDSCRIPT) \
		-Wl,-Map=../$(SINGLE_NAME).map \
		-o ../$(SINGLE_NAME) \
		-I../.. \
		-Os -Wall -lm \
		$(addprefix ../,$(notdir $(SINGLE_OBJS))) \
		$(LDFLAGS))

$(BUILD_DIR)/$(SINGLE_NAME).pgz: $(BUILD_DIR)/$(SINGLE_NAME)
	@# If linker produced an ELF variant, use it for symbol/listing; otherwise skip.
	@if [ -f $(BUILD_DIR)/$(SINGLE_NAME).elf ]; then \
		ELF_FILE=$(BUILD_DIR)/$(SINGLE_NAME).elf; \
	elif [ -f $(BUILD_DIR)/$(SINGLE_NAME).elf.elf ]; then \
		ELF_FILE=$(BUILD_DIR)/$(SINGLE_NAME).elf.elf; \
	else \
		ELF_FILE=; \
	fi; \
	if [ -n "$$ELF_FILE" ]; then \
		$(NM) "$$ELF_FILE" > $(BUILD_DIR)/$(SINGLE_NAME).sym || true; \
		$(OBJDUMP) --syms -d --print-imm-hex "$$ELF_FILE" > $(BUILD_DIR)/$(SINGLE_NAME).lst || true; \
	fi

	# If linker produced a raw binary named '$(SINGLE_NAME)', promote it to .pgz as f256build.sh did
	@if [ -f $(BUILD_DIR)/$(SINGLE_NAME) ]; then \
		mv $(BUILD_DIR)/$(SINGLE_NAME) $(BUILD_DIR)/$(SINGLE_NAME).pgz; \
	fi

	# If a .pgz was created by overlay or the linker, run pgz-thunk to inspect it
	@if [ -f $(BUILD_DIR)/$(SINGLE_NAME).pgz ]; then \
		SCRIPT=""; \
		if [ -f "$(PGZ_THUNK)" ]; then \
			SCRIPT="$(PGZ_THUNK)"; \
		elif command -v $(PGZ_THUNK) >/dev/null 2>&1; then \
			SCRIPT="$$(command -v $(PGZ_THUNK))"; \
		fi; \
		if [ -n "$$SCRIPT" ]; then \
			$(PYTHON) "$$SCRIPT" $(BUILD_DIR)/$(SINGLE_NAME).pgz || echo "pgz-thunk did not process $(BUILD_DIR)/$(SINGLE_NAME).pgz"; \
		fi; \
	fi

	# Ensure a PGZ artifact was produced by overlay/pgz-thunk.
	@if [ ! -f $(BUILD_DIR)/$(SINGLE_NAME).pgz ]; then \
		echo "ERROR: pgz-thunk did not produce $(BUILD_DIR)/$(SINGLE_NAME).pgz" >&2; \
		exit 1; \
	fi
	@mkdir -p bin
	@cp $(BUILD_DIR)/$(SINGLE_NAME).pgz bin/

# Multi-object scene demo (mirrors single_object pattern)
MULTI_NAME := multi_object_scene
MULTI_SRC := examples/multi_object_scene.c
MULTI_OBJ := $(BUILD_DIR)/$(MULTI_NAME).o
MULTI_ASM := $(BUILD_DIR)/$(MULTI_NAME).s
MULTI_DEPS := $(BUILD_DIR)/3d_object.o $(BUILD_DIR)/draw_line.o $(BUILD_DIR)/draw_lines_asm.o $(BUILD_DIR)/emit_edges_asm.o $(BUILD_DIR)/geometry_kernel.o $(BUILD_DIR)/video.o $(BUILD_DIR)/vs1053b.o
MULTI_OBJS := $(MULTI_OBJ) $(MULTI_DEPS)
# Separate overlay output dir for multi_object_scene (only plugin_data, no other assets)
MULTI_OVERLAY_DIR := $(BUILD_DIR)/mo
MULTI_SRC_STAGING := $(BUILD_DIR)/mo_src

.PHONY: $(MULTI_NAME) multi_object_overlay
$(MULTI_NAME): $(BUILD_DIR)/$(MULTI_NAME).pgz
	@echo "$(MULTI_NAME) build complete: $<"

# Overlay scoped to only vs1053b.c — produces output.ld with just plugin_data segment
multi_object_overlay: dirs
	@mkdir -p $(MULTI_SRC_STAGING)/assets $(MULTI_OVERLAY_DIR)/assets
	@cp $(ROOT)/src/vs1053b.c $(MULTI_SRC_STAGING)/
	@cp -a $(ROOT)/src/assets/* $(MULTI_SRC_STAGING)/assets/ 2>/dev/null || true
	@cp -a $(ROOT)/assets/* $(MULTI_SRC_STAGING)/assets/ 2>/dev/null || true
	@cp -a $(ROOT)/src/assets/* $(MULTI_OVERLAY_DIR)/assets/ 2>/dev/null || true
	@cp -a $(ROOT)/assets/* $(MULTI_OVERLAY_DIR)/assets/ 2>/dev/null || true
	@if command -v $(OVERLAY) >/dev/null 2>&1; then \
		$(OVERLAY) 5 $(MULTI_OVERLAY_DIR) $(MULTI_SRC_STAGING) || { echo "multi overlay failed" >&2; exit 1; }; \
	else \
		echo "overlay not found; skipping multi overlay step"; \
	fi

$(BUILD_DIR)/$(MULTI_NAME).o: $(MULTI_SRC) | dirs
	$(CC) -c $(CFLAGS) -o $@ $<

$(BUILD_DIR)/$(MULTI_NAME).s: $(MULTI_SRC) | dirs
	$(CC) -S $(CFLAGS) -o $@ $<

$(BUILD_DIR)/$(MULTI_NAME): multi_object_overlay $(MULTI_OBJS) | dirs
	(cd $(MULTI_OVERLAY_DIR) && \
	$(CC) \
		-D__llvm_mos__ \
		-T ../../$(LDSCRIPT) \
		-Wl,-Map=../$(MULTI_NAME).map \
		-o ../$(MULTI_NAME) \
		-I../.. \
		-Os -Wall -lm \
		$(addprefix ../,$(notdir $(MULTI_OBJS))) \
		$(LDFLAGS))

$(BUILD_DIR)/$(MULTI_NAME).pgz: $(BUILD_DIR)/$(MULTI_NAME)
	@if [ -f $(BUILD_DIR)/$(MULTI_NAME).elf ]; then \
		ELF_FILE=$(BUILD_DIR)/$(MULTI_NAME).elf; \
	elif [ -f $(BUILD_DIR)/$(MULTI_NAME).elf.elf ]; then \
		ELF_FILE=$(BUILD_DIR)/$(MULTI_NAME).elf.elf; \
	else \
		ELF_FILE=; \
	fi; \
	if [ -n "$$ELF_FILE" ]; then \
		$(NM) "$$ELF_FILE" > $(BUILD_DIR)/$(MULTI_NAME).sym || true; \
		$(OBJDUMP) --syms -d --print-imm-hex "$$ELF_FILE" > $(BUILD_DIR)/$(MULTI_NAME).lst || true; \
	fi

	@if [ -f $(BUILD_DIR)/$(MULTI_NAME) ]; then \
		mv $(BUILD_DIR)/$(MULTI_NAME) $(BUILD_DIR)/$(MULTI_NAME).pgz; \
	fi

	@if [ -f $(BUILD_DIR)/$(MULTI_NAME).pgz ]; then \
		SCRIPT=""; \
		if [ -f "$(PGZ_THUNK)" ]; then \
			SCRIPT="$(PGZ_THUNK)"; \
		elif command -v $(PGZ_THUNK) >/dev/null 2>&1; then \
			SCRIPT="$$(command -v $(PGZ_THUNK))"; \
		fi; \
		if [ -n "$$SCRIPT" ]; then \
			$(PYTHON) "$$SCRIPT" $(BUILD_DIR)/$(MULTI_NAME).pgz || echo "pgz-thunk did not process $(BUILD_DIR)/$(MULTI_NAME).pgz"; \
		fi; \
	fi

	# Ensure a PGZ artifact was produced by overlay/pgz-thunk.
	@if [ ! -f $(BUILD_DIR)/$(MULTI_NAME).pgz ]; then \
		echo "ERROR: pgz-thunk did not produce $(BUILD_DIR)/$(MULTI_NAME).pgz" >&2; \
		exit 1; \
	fi
	@mkdir -p bin
	@cp $(BUILD_DIR)/$(MULTI_NAME).pgz bin/

.PHONY: examples
examples: single_object multi_object_scene
	@echo "Built examples: bin/single_object.pgz bin/multi_object_scene.pgz"


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

