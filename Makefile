# Fugazi — Leaf CRT shader tuner.
#
# Leaf stages this app with `make package-platform PLATFORM=mlp1`, which builds
# the aarch64 binary in the mlp1-toolchain container and assembles the staged
# pak under build/<platform>/package/Fugazi.pak (Leaf then deploys that dir).

MLP1_PACKAGE := build/mlp1/package/Fugazi.pak
MLP1_BIN     := ports/mlp1/pak/bin/fugazi
MLP1_BUILD_PROFILE ?= release
WORKSPACE_ROOT ?= $(abspath ..)
MLP1_FLAGS_MK ?= $(firstword $(wildcard /opt/mlp1-toolchain/umrk/mlp1-build-flags.mk $(WORKSPACE_ROOT)/mlp1-toolchain/flags/mlp1-build-flags.mk ../mlp1-toolchain/flags/mlp1-build-flags.mk))
ifneq ($(MLP1_FLAGS_MK),)
include $(MLP1_FLAGS_MK)
else
UMRK_MLP1_TARGET_SOC ?= rk3566
UMRK_MLP1_TARGET_CPU ?= cortex-a55
UMRK_MLP1_PROFILE_CFLAGS ?= -O2 -mcpu=cortex-a55 -mtune=cortex-a55 -ffunction-sections -fdata-sections -DNDEBUG
UMRK_MLP1_PROFILE_LDFLAGS ?= -Wl,--gc-sections
endif

.PHONY: package-platform package-mlp1 mlp1 preset-ownership-test i18n-pot i18n-build i18n-check clean

i18n-pot:
	python3 tools/i18n-extract.py

i18n-build:
	python3 tools/i18n-po2tsv.py i18n/zh_CN.po -o build/i18n/zh_CN.tsv

i18n-check: i18n-build
	python3 tools/i18n-extract.py --check --po i18n/zh_CN.po --gate 90

# Native test for the global-preset ownership module. No SDL, no GL, no device:
# it runs on the host against tests/fixtures/global-preset.
HOST_CC ?= cc
PRESET_TEST_BIN := build/preset-ownership-test

preset-ownership-test:
	@mkdir -p build
	$(HOST_CC) -std=gnu11 -Wall -Wextra -Werror -O1 -g -Icmd/fugazi \
		-o $(PRESET_TEST_BIN) tests/preset_ownership_test.c cmd/fugazi/preset_ownership.c
	@./$(PRESET_TEST_BIN) tests/fixtures/global-preset

package-platform:
	@test -n "$(PLATFORM)" || { echo "usage: make package-platform PLATFORM=<platform>" >&2; exit 1; }
	@case "$(PLATFORM)" in \
		mlp1) $(MAKE) package-mlp1 ;; \
		*) echo "unsupported Fugazi package platform: $(PLATFORM)" >&2; exit 1 ;; \
	esac

# Cross-compile the aarch64 binary (Docker mlp1-toolchain).
mlp1:
	@MLP1_BUILD_PROFILE="$(MLP1_BUILD_PROFILE)" ./scripts/build-mlp1.sh

# Build, then assemble the staged pak: pak/ template + the built binary.
package-mlp1: mlp1 i18n-check
	@rm -rf "$(MLP1_PACKAGE)"
	@mkdir -p "$(MLP1_PACKAGE)/bin" "$(MLP1_PACKAGE)/res/i18n"
	@cp -R pak/launch.sh pak/pak.json pak/res pak/shaders "$(MLP1_PACKAGE)/"
	@cp "$(MLP1_BIN)" "$(MLP1_PACKAGE)/bin/fugazi"
	@cp build/i18n/*.tsv "$(MLP1_PACKAGE)/res/i18n/"
	@{ \
		printf '{\n'; \
		printf '  "platform": "mlp1",\n'; \
		printf '  "target_soc": "%s",\n' "$(UMRK_MLP1_TARGET_SOC)"; \
		printf '  "target_cpu": "%s",\n' "$(UMRK_MLP1_TARGET_CPU)"; \
		printf '  "build_profile": "%s",\n' "$(MLP1_BUILD_PROFILE)"; \
		printf '  "cflags": "%s",\n' "$(UMRK_MLP1_PROFILE_CFLAGS)"; \
		printf '  "ldflags": "%s",\n' "$(UMRK_MLP1_PROFILE_LDFLAGS)"; \
		printf '  "binaries": ["bin/fugazi"],\n'; \
		printf '  "exceptions": []\n'; \
		printf '}\n'; \
	} > "$(MLP1_PACKAGE)/build-manifest.json"
	@echo "=== Packaged: $(MLP1_PACKAGE) ==="

clean:
	@rm -rf build ports/mlp1/pak/bin dist
