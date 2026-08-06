# Makefile for SHIRE EPS component CLI development
.PHONY: all build clean debug runtime start stop

export BUILDDIR ?= $(CURDIR)/build
export BUILD_IMAGE ?= ghcr.io/voyagertechnologies-natsec/shire-base:latest
export RUNTIME_NAME ?= shire-comp-eps-cli
export SHIRE_DIR ?= $(CURDIR)/../../..
export TGTNAME ?= cpu1

# Determine number of parallel jobs to avoid maxing out low-power systems (Raspberry Pi etc.).
# Use `nproc - 1` but ensure at least 1 job.
NPROC := $(shell nproc 2>/dev/null || echo 1)
JOBS := $(shell if [ $(NPROC) -le 1 ]; then echo 1; else expr $(NPROC) - 1; fi)

# Commands
all: runtime

build:
	docker run --rm -v $(SHIRE_DIR):$(SHIRE_DIR) --name $(RUNTIME_NAME) -w $(CURDIR) --user $(shell id -u):$(shell id -g) -e BUILDDIR=$(BUILDDIR) $(BUILD_IMAGE) make -j$(JOBS) build-cli

build-cli:
	mkdir -p $(BUILDDIR) && \
	cd $(BUILDDIR) && cmake $(CURDIR) -DTGTNAME=$(TGTNAME) && \
	$(MAKE) --no-print-directory -C $(BUILDDIR)

clean:
	rm -rf $(BUILDDIR)

debug:
	docker run --rm -it -v $(SHIRE_DIR):$(SHIRE_DIR) --name $(RUNTIME_NAME) -w $(CURDIR) --user $(shell id -u):$(shell id -g) $(BUILD_IMAGE) /bin/bash

runtime:
	$(MAKE) clean build && docker build -t $(RUNTIME_NAME) -f ../support/Dockerfile.cli --build-arg BUILDDIR=$$(realpath --relative-to=$(SHIRE_DIR) $(BUILDDIR)) $(SHIRE_DIR)

start:
	docker run --rm -it -v $(SHIRE_DIR):$(SHIRE_DIR) --name "shire_comp_run" $(BUILD_IMAGE) ./$(BUILDDIR)/eps_cli

stop:
	docker ps --filter name=shire-* | xargs docker stop
