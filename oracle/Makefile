# Oracle Pipeline Makefile
# AMOS Reborn — automated FS-UAE capture and comparison
#
# Usage:
#   make -f oracle/Makefile oracle-setup       # check prerequisites
#   make -f oracle/Makefile oracle-generate     # capture all reference PNGs via FS-UAE
#   make -f oracle/Makefile oracle-test         # run reborn + compare against reference
#   make -f oracle/Makefile oracle-report       # generate summary report
#   make -f oracle/Makefile oracle-clean        # remove output/ directory

PROJECT_ROOT := $(shell cd $(dir $(lastword $(MAKEFILE_LIST)))/.. && pwd)
ORACLE_DIR   := $(PROJECT_ROOT)/oracle
CONF         := $(ORACLE_DIR)/config/oracle.conf
SCRIPTS      := $(ORACLE_DIR)/scripts
PROGRAMS     := $(ORACLE_DIR)/programs
OUTPUT       := $(ORACLE_DIR)/output
REFERENCE    := $(ORACLE_DIR)/reference

# Load config values
-include $(CONF)

# Find all test programs
AMOS_SOURCES := $(wildcard $(PROGRAMS)/*.amos)
AMOS_NAMES   := $(basename $(notdir $(AMOS_SOURCES)))

# Oracle output PNGs (one per test program)
ORACLE_PNGS  := $(addprefix $(OUTPUT)/oracle/,$(addsuffix .png,$(AMOS_NAMES)))
REBORN_PNGS  := $(addprefix $(OUTPUT)/reborn/,$(addsuffix .png,$(AMOS_NAMES)))
DIFF_PNGS    := $(addprefix $(OUTPUT)/diffs/,$(addsuffix .png,$(AMOS_NAMES)))

.PHONY: oracle-setup oracle-capture-build oracle-generate oracle-test oracle-report oracle-clean oracle-help

oracle-help:
	@echo "Oracle Pipeline — AMOS Reborn"
	@echo ""
	@echo "Targets:"
	@echo "  oracle-setup          Check all prerequisites"
	@echo "  oracle-capture-build  Build the oracle-capture tool"
	@echo "  oracle-generate       Run FS-UAE for all test programs, save reference PNGs"
	@echo "  oracle-test           Run reborn for all test programs, compare against reference"
	@echo "  oracle-report         Generate comparison summary"
	@echo "  oracle-clean          Remove output/ directory"
	@echo ""
	@echo "Test programs found: $(words $(AMOS_SOURCES))"
	@echo "  $(AMOS_NAMES)"

# ─── Setup ────────────────────────────────────────────────────────────────────

oracle-setup:
	@bash $(SCRIPTS)/setup_check.sh

# ─── Build oracle-capture ────────────────────────────────────────────────────

oracle-capture-build:
	@echo "Building oracle-capture..."
	@mkdir -p $(PROJECT_ROOT)/build
	@cd $(PROJECT_ROOT)/build && cmake .. && make oracle-capture
	@echo "Done."

# ─── Generate reference PNGs from FS-UAE ─────────────────────────────────────

oracle-generate: oracle-setup
	@echo "=== Generating oracle reference screenshots ==="
	@mkdir -p $(OUTPUT)/oracle
	@for prog in $(AMOS_SOURCES); do \
		name=$$(basename "$$prog" .amos); \
		echo "--- Capturing: $$name ---"; \
		bash $(SCRIPTS)/fsuae_capture.sh "$$prog" || \
			echo "WARNING: Capture failed for $$name"; \
		echo ""; \
	done
	@echo "=== Oracle generation complete ==="
	@echo "Screenshots in: $(OUTPUT)/oracle/"
	@ls -la $(OUTPUT)/oracle/*.png 2>/dev/null || echo "  (no screenshots captured)"

# ─── Run reborn and compare ──────────────────────────────────────────────────

oracle-test: oracle-setup
	@echo "=== Running AMOS Reborn oracle comparison ==="
	@mkdir -p $(OUTPUT)/reborn $(OUTPUT)/diffs
	@PASS=0; FAIL=0; SKIP=0; \
	for prog in $(AMOS_SOURCES); do \
		name=$$(basename "$$prog" .amos); \
		oracle_png="$(OUTPUT)/oracle/$$name.png"; \
		reborn_png="$(OUTPUT)/reborn/$$name.png"; \
		diff_png="$(OUTPUT)/diffs/$$name.png"; \
		if [ ! -f "$$oracle_png" ]; then \
			echo "  SKIP $$name (no oracle reference)"; \
			SKIP=$$((SKIP + 1)); \
			continue; \
		fi; \
		echo "  TEST $$name..."; \
		if [ -x "$(PROJECT_ROOT)/$(REBORN_BIN)" ]; then \
			$(PROJECT_ROOT)/$(REBORN_BIN) "$$prog" --screenshot "$$reborn_png" 2>/dev/null; \
		else \
			echo "    WARNING: oracle-capture binary not built, skipping reborn capture"; \
			SKIP=$$((SKIP + 1)); \
			continue; \
		fi; \
		if [ -f "$$reborn_png" ]; then \
			python3 -c " \
from PIL import Image, ImageChops; \
import sys; \
a = Image.open('$$oracle_png'); \
b = Image.open('$$reborn_png'); \
if a.size != b.size: \
    b = b.resize(a.size); \
diff = ImageChops.difference(a, b); \
diff.save('$$diff_png'); \
pixels = list(diff.getdata()); \
total = len(pixels); \
changed = sum(1 for p in pixels if (sum(p[:3]) if isinstance(p, tuple) else p) > 10); \
pct = (changed / total) * 100; \
print(f'    Diff: {pct:.1f}% pixels changed'); \
sys.exit(0 if pct < 5.0 else 1) \
" 2>/dev/null; \
			if [ $$? -eq 0 ]; then \
				echo "    PASS"; \
				PASS=$$((PASS + 1)); \
			else \
				echo "    FAIL (visual difference > 5%)"; \
				FAIL=$$((FAIL + 1)); \
			fi; \
		else \
			echo "    FAIL (reborn produced no screenshot)"; \
			FAIL=$$((FAIL + 1)); \
		fi; \
	done; \
	echo ""; \
	echo "=== Results: $$PASS passed, $$FAIL failed, $$SKIP skipped ==="

# ─── Report ──────────────────────────────────────────────────────────────────

oracle-report:
	@echo "=== Oracle Pipeline Report ==="
	@echo ""
	@echo "Test Programs:"
	@for prog in $(AMOS_SOURCES); do \
		name=$$(basename "$$prog" .amos); \
		oracle_png="$(OUTPUT)/oracle/$$name.png"; \
		reborn_png="$(OUTPUT)/reborn/$$name.png"; \
		diff_png="$(OUTPUT)/diffs/$$name.png"; \
		printf "  %-40s " "$$name"; \
		if [ -f "$$oracle_png" ]; then printf "oracle:YES "; else printf "oracle:NO  "; fi; \
		if [ -f "$$reborn_png" ]; then printf "reborn:YES "; else printf "reborn:NO  "; fi; \
		if [ -f "$$diff_png" ]; then printf "diff:YES"; else printf "diff:NO "; fi; \
		echo ""; \
	done
	@echo ""
	@echo "Oracle screenshots:  $(OUTPUT)/oracle/"
	@echo "Reborn screenshots:  $(OUTPUT)/reborn/"
	@echo "Diff images:         $(OUTPUT)/diffs/"

# ─── Clean ───────────────────────────────────────────────────────────────────

oracle-clean:
	@echo "Removing oracle output directory..."
	rm -rf $(OUTPUT)/oracle/*.png $(OUTPUT)/reborn/*.png $(OUTPUT)/diffs/*.png
	rm -rf $(ORACLE_DIR)/staging/*
	@echo "Done. Reference images in $(REFERENCE)/ are preserved."
