# Makefile for SHIRE Flight Software (FSW) development
.PHONY: all build clean debug runtime start stop test

export BUILD_IMAGE ?= ghcr.io/voyagertechnologies-public/shire-base:latest
export BUILDDIR ?= $(CURDIR)/build
export BUILDTYPE ?= debug
export CFS_APP_PATH = ../comp
export COVDIR ?= $(BUILDDIR)/amd64-linux/default_cpu1
export INSTALLPREFIX ?= exe
export MISSION ?= drm
export SPACECRAFT ?= sat-1
export RUNTIME_FSW_IMAGE_NAME ?= shire-fsw-$(MISSION)
export SHIRE_DIR ?= $(CURDIR)/..

SPACECRAFT_CFG_DIR := ../build/$(MISSION)/$(SPACECRAFT)
export MISSION_DEFS ?= $(SPACECRAFT_CFG_DIR)
export MISSIONCONFIG ?= $(SPACECRAFT_CFG_DIR)/shire

# Determine number of parallel jobs to avoid maxing out low-power systems (Raspberry Pi etc.).
# Use `nproc - 1` but ensure at least 1 job.
NPROC := $(shell nproc 2>/dev/null || echo 1)
JOBS := $(shell if [ $(NPROC) -le 1 ]; then echo 1; else expr $(NPROC) - 1; fi)

# The "prep" step requires extra options that are specified via environment variables
PREP_OPTS :=
ifneq ($(INSTALLPREFIX),)
PREP_OPTS += -DCMAKE_INSTALL_PREFIX=$(INSTALLPREFIX)
endif
ifneq ($(VERBOSE),)
PREP_OPTS += --trace
endif
ifneq ($(BUILDTYPE),)
PREP_OPTS += -DCMAKE_BUILD_TYPE=$(BUILDTYPE)
endif

# Commands
all: build

build:
	docker run --rm -it -v $(SHIRE_DIR):$(SHIRE_DIR) --name "shire_fsw_build" -w $(CURDIR) --user $(shell id -u):$(shell id -g) --sysctl fs.mqueue.msg_max=10000 --ulimit rtprio=99 --cap-add=sys_nice -e BUILDDIR=$(BUILDDIR) -e SPACECRAFT=$(SPACECRAFT) $(BUILD_IMAGE) make -j$(JOBS) build-fsw

build-fsw:
	mkdir -p $(BUILDDIR) && \
	cd $(BUILDDIR) && cmake $(PREP_OPTS) $(CURDIR)/cfe && \
	$(MAKE) --no-print-directory -C $(BUILDDIR) mission-install

build-test:
	mkdir -p $(BUILDDIR) && \
	cd $(BUILDDIR) && cmake $(PREP_OPTS) -DENABLE_UNIT_TESTS=true $(CURDIR)/cfe && \
	$(MAKE) --no-print-directory -C $(BUILDDIR) mission-install && \
	cd $(COVDIR) && ctest --output-on-failure -O ctest.log
	lcov -c --directory . --output-file $(COVDIR)/coverage.info
	lcov --remove $(COVDIR)/coverage.info '*/unit-test/stubs/*' '*/unit-test/coveragetest/*' -o $(COVDIR)/coverage_filtered.info
	genhtml $(COVDIR)/coverage_filtered.info --output-directory $(COVDIR)/report
	@echo ""
	@echo "Review coverage report: "
	@echo "  firefox $(COVDIR)/report/index.html"
	@echo ""

clean:
	rm -rf $(BUILDDIR)

debug:
	docker run --rm -it -v $(SHIRE_DIR):$(SHIRE_DIR) --name "shire_fsw_debug" -w $(CURDIR) --user $(shell id -u):$(shell id -g) --sysctl fs.mqueue.msg_max=10000 --ulimit rtprio=99 --cap-add=sys_nice $(BUILD_IMAGE) /bin/bash

runtime:
	$(MAKE) clean build
	@mkdir -p ../build/$(MISSION)/$(SPACECRAFT)/lib
	@if [ -f "../build/$(MISSION)/$(SPACECRAFT)/sim/libsimulith.so" ]; then \
		cp "../build/$(MISSION)/$(SPACECRAFT)/sim/libsimulith.so" "../build/$(MISSION)/$(SPACECRAFT)/lib/libsimulith.so"; \
	elif [ -f "../build/$(MISSION)/$(SPACECRAFT)/fsw/amd64-shire/default_cpu1/simulith/libsimulith.so" ]; then \
		cp "../build/$(MISSION)/$(SPACECRAFT)/fsw/amd64-shire/default_cpu1/simulith/libsimulith.so" "../build/$(MISSION)/$(SPACECRAFT)/lib/libsimulith.so"; \
	fi
	cd .. && docker build -t $(RUNTIME_FSW_IMAGE_NAME):$(SPACECRAFT) -f ${CURDIR}/tools/Dockerfile.fsw --build-arg SPACECRAFT=$(SPACECRAFT) --build-arg MISSION=$(MISSION) .

start:
	docker run --rm -it --name "shire_fsw_runtime" --sysctl fs.mqueue.msg_max=10000 --ulimit rtprio=99 --cap-add=sys_nice $(RUNTIME_FSW_IMAGE_NAME)

stop:
	docker ps --filter name=shire-* --filter status=running -aq | xargs docker stop

test:
	docker run --rm -it -v $(SHIRE_DIR):$(SHIRE_DIR) --name "shire_fsw_build" -w $(CURDIR) --user $(shell id -u):$(shell id -g) --sysctl fs.mqueue.msg_max=10000 --ulimit rtprio=99 --cap-add=sys_nice -e BUILDDIR=$(BUILDDIR) -e SPACECRAFT=$(SPACECRAFT) $(BUILD_IMAGE) make -j$(JOBS) build-test
