# Compiler
CC := mpicc

# Compiler flags
CFLAGS := -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 \
	-Wconversion -Wsign-conversion -Wnull-dereference \
	-Wdouble-promotion -Werror -O2 -g

# Linker flags
LDFLAGS := -lm

# Directories
SRC_DIR   := src
BIN_DIR   := bin
SLURM_DIR := slurm
RUNS_DIR  := runs

# Absolute path for Slurm
PWD_ABS := $(shell pwd)

# Formatter (use clang-format if available, otherwise skip)
FORMATTER := $(shell command -v clang-format 2>/dev/null)

# Get extra source files from command line (all args after first)
EXTRA_SRCS = $(wordlist 2,$(words $(MAKECMDGOALS)),$(MAKECMDGOALS))
EXTRA_FILES = $(addprefix $(SRC_DIR)/,$(addsuffix .c,$(EXTRA_SRCS)))

# -------------------------------
# Main build rule
# -------------------------------
%: $(SRC_DIR)/%.c | $(BIN_DIR) $(SLURM_DIR) $(RUNS_DIR)/%
	@if [ -n "$(FORMATTER)" ]; then $(FORMATTER) -i $<; fi
	$(CC) $(CFLAGS) $< $(EXTRA_FILES) -o $(BIN_DIR)/$@ $(LDFLAGS)
	@echo "Generated binary: $(BIN_DIR)/$@"
	@$(MAKE) --no-print-directory slurm/$@

# -------------------------------
# Slurm file generation
# -------------------------------
slurm/%:
	@printf '%s\n' \
		'#!/bin/bash' \
		'#SBATCH --job-name=$*' \
		'#SBATCH --partition=full' \
		'#SBATCH --nodes=10' \
		'#SBATCH --ntasks=10' \
		'#SBATCH --ntasks-per-node=1' \
		'#SBATCH --time=00:10:00' \
		'#SBATCH --output=/mnt/gv0/$(PWD_ABS)/$(RUNS_DIR)/$*/%j/log' \
		'#SBATCH --error=/mnt/gv0/$(PWD_ABS)/$(RUNS_DIR)/$*/%j/error' \
		'' \
		'echo "Nodes allocated: $$SLURM_NODELIST"' \
		'echo "Total tasks: $$SLURM_NTASKS"' \
		'' \
		'srun --mpi=pmix /mnt/gv0/$(PWD_ABS)/$(BIN_DIR)/$*' \
		> $(SLURM_DIR)/$*.slurm
	@chmod +x $(SLURM_DIR)/$*.slurm
	@echo "Generated Slurm script: $(SLURM_DIR)/$*.slurm"

# -------------------------------
# Directory creation
# -------------------------------
$(BIN_DIR):
	@mkdir -p $(BIN_DIR)

$(SLURM_DIR):
	@mkdir -p $(SLURM_DIR)

$(RUNS_DIR)/%:
	@mkdir -p $(RUNS_DIR)/$*

# -------------------------------
# Dummy targets for extra source files
# -------------------------------
# $(EXTRA_SRCS):
# 	@:

# -------------------------------
# Cleanup
# -------------------------------
clean:
	rm -rf $(BIN_DIR)/*

.PHONY: clean $(EXTRA_SRCS)