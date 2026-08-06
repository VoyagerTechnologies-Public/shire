# Makefile for SHIRE eps component SIM development
.PHONY: all build build-sim clean debug

export BUILDDIR ?= $(CURDIR)/build
export BUILD_IMAGE ?= ghcr.io/voyagertechnologies-natsec/shire-base:latest
export RUNTIME_NAME ?= shire-comp-eps-sim
export SHIRE_DIR ?= $(CURDIR)/../../..

# Determine number of parallel jobs to avoid maxing out low-power systems (Raspberry Pi etc.).
# Use `nproc - 1` but ensure at least 1 job.
NPROC := $(shell nproc 2>/dev/null || echo 1)
JOBS := $(shell if [ $(NPROC) -le 1 ]; then echo 1; else expr $(NPROC) - 1; fi)

# Commands
all: build

build:
	docker run --rm -it -v $(SHIRE_DIR):$(SHIRE_DIR) --name $(RUNTIME_NAME) -w $(CURDIR) --user $(shell id -u):$(shell id -g) -e BUILDDIR=$(BUILDDIR) $(BUILD_IMAGE) make -j$(JOBS) build-sim

build-sim:
	mkdir -p $(BUILDDIR)
	cd $(BUILDDIR) && cmake $(CURDIR)
	$(MAKE) --no-print-directory -C $(BUILDDIR)

clean:
	docker run --rm -it -v $(CURDIR):$(CURDIR) --name $(RUNTIME_NAME) -w $(CURDIR) $(BUILD_IMAGE) rm -rf $(BUILDDIR)

debug:
	docker run --rm -it -v $(SHIRE_DIR):$(SHIRE_DIR) --name $(RUNTIME_NAME) -w $(CURDIR) --user $(shell id -u):$(shell id -g) $(BUILD_IMAGE) /bin/bash
