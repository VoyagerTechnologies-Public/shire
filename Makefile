# Makefile for SHIRE development
.PHONY: 42 build clean clean-42 clean-cache clean-cli clean-fsw clean-gsw clean-sim cfg cfg-cli cli cli-start container debug fsw gsw help mold sim start stop test-fsw test-sim uninstall
.DEFAULT_GOAL := build

# Build image name
export BUILD_IMAGE ?= ghcr.io/voyagertechnologies-public/shire-base:latest

# Common paths
CFG_DIR := $(CURDIR)/cfg

# Read spacecraft, mission, fsw, and gsw from active.yaml for unified build directory structure
BUILD_DIR := $(CURDIR)/build
SPACECRAFT := $(shell grep '^spacecraft:' $(BUILD_DIR)/active.yaml 2>/dev/null | sed 's/spacecraft: *//' | tr -d ' ')
MISSION := $(shell grep '^mission:' $(BUILD_DIR)/active.yaml 2>/dev/null | sed 's/mission: *//' | tr -d ' ')
FSW_DIR := $(shell grep '^fsw_dir:' $(BUILD_DIR)/active.yaml 2>/dev/null | sed 's/fsw_dir: *//' | tr -d ' ')
GSW_DIR := $(shell grep '^gsw_dir:' $(BUILD_DIR)/active.yaml 2>/dev/null | sed 's/gsw_dir: *//' | tr -d ' ')
FSW_DIR := $(if $(FSW_DIR),$(FSW_DIR),cfs)
GSW_DIR := $(if $(GSW_DIR),$(GSW_DIR),yamcs)
export FSW_DIR GSW_DIR
ifneq ($(SPACECRAFT),)
ifneq ($(MISSION),)
export BUILDDIR_MISSION := $(BUILD_DIR)/$(MISSION)/
export BUILDDIR_BASE := $(BUILDDIR_MISSION)/$(SPACECRAFT)
export BUILDDIR_SIM := $(BUILDDIR_BASE)/sim
export BUILDDIR_FSW := $(BUILDDIR_BASE)/fsw
export BUILDDIR_GSW := $(BUILDDIR_BASE)/gsw
export BUILDDIR_COMP := $(BUILDDIR_BASE)/comp
endif
endif

# Commands
42: cfg
	python3 cfg/shire-build.py 42

build: cfg
	python3 cfg/shire-build.py build

cfg: container
	docker run --rm -v $(CURDIR):$(CURDIR) -w $(CURDIR)/cfg --user $(shell id -u):$(shell id -g) $(BUILD_IMAGE) python3 shire-orchestrator.py

cfg-cli: container
	docker run --rm -v $(CURDIR):$(CURDIR) -w $(CURDIR)/cfg --user $(shell id -u):$(shell id -g) $(BUILD_IMAGE) python3 shire-orchestrator.py --cli-debug

clean:
	$(MAKE) stop
	@if docker image inspect $(BUILD_IMAGE) >/dev/null 2>&1; then \
		$(MAKE) clean-42; \
		rm -rf $(BUILDDIR_MISSION); \
		$(MAKE) clean-gsw; \
		docker volume ls -q --filter "name=gsw-data" | xargs -r docker volume rm; \
		docker volume ls -q --filter "name=simulith_ipc" | xargs -r docker volume rm; \
	else \
		echo "Docker image $(BUILD_IMAGE) does not exist. Skipping clean subcommands."; \
	fi

clean-cache:
	docker builder prune -f
	docker volume rm -f gsw-data simulith_ipc || true

clean-42:
	python3 cfg/shire-build.py clean-42

clean-cli:
	python3 cfg/shire-build.py clean-cli

clean-fsw:
	cd $(FSW_DIR) && $(MAKE) clean

clean-gsw:
	cd $(GSW_DIR) && $(MAKE) clean

clean-sim:
	python3 cfg/shire-build.py clean-sim

cli: cfg-cli
	python3 cfg/shire-build.py cli

cli-start: cfg
	docker compose -f $(BUILDDIR_MISSION)/cli-compose.yaml up

container: .container.stamp

.container.stamp: cfg/Dockerfile.base cfg/requirements.txt
	@command -v docker >/dev/null 2>&1 || { echo "Error: docker is not installed or not in PATH."; exit 1; }
	docker build -t $(BUILD_IMAGE) -f cfg/Dockerfile.base \
		--build-arg USER_ID=$(shell id -u) --build-arg GROUP_ID=$(shell id -g) cfg
	@touch .container.stamp

debug: cfg
	docker run --rm -it -v $(CURDIR):$(CURDIR) --name "shire_fsw_debug" -w $(CURDIR) --user $(shell id -u):$(shell id -g) --sysctl fs.mqueue.msg_max=10000 --ulimit rtprio=99 --cap-add=sys_nice $(BUILD_IMAGE) /bin/bash
	
fsw: cfg
	python3 cfg/shire-build.py fsw

gsw: cfg
	python3 cfg/shire-build.py gsw

mold:
	@if [ "$(COMP)" = "" ]; then \
		echo "Error: COMP parameter is required"; \
		echo "Usage: make mold COMP=<name>"; \
		echo "Example: make mold COMP=my_sensor"; \
		exit 1; \
	fi
	python3 $(CFG_DIR)/shire-comp-mold.py "$(COMP)"

help:
	@echo "Usage: make <target>"
	@echo "Targets:"
	@echo "  42            - Build 42 simulator container"
	@echo "  build         - Build the full runtime environment"
	@echo "  cfg           - Run orchestrator to configure environment"
	@echo "  cfg-cli       - Run orchestrator with debug=true for CLI builds"
	@echo "  cli           - Build CLI components"
	@echo "  cli-start     - Start CLI compose"
	@echo "  clean         - Remove build artifacts and stop compose"
	@echo "  clean-42      - Clean 42 simulator container"
	@echo "  clean-cache   - Clean Docker build cache (frees significant disk space)"
	@echo "  clean-cli     - Clean CLI components"
	@echo "  clean-fsw     - Clean FSW components"
	@echo "  clean-gsw     - Clean GSW components"
	@echo "  clean-sim     - Clean simulation components"
	@echo "  container     - Build the Docker container"
	@echo "  debug         - Start a debug shell in the container"
	@echo "  fsw           - Build FSW (includes Docker image)"
	@echo "  gsw           - Build GSW (includes Docker image)"
	@echo "  list          - List enabled components from configuration"
	@echo "  mold          - Create new component from demo template (Usage: make mold COMP=<name>)"
	@echo "  sim           - Build Simulith and component simulators (includes Docker images)"
	@echo "  start         - Start lab compose"
	@echo "  stop          - Stop lab and CLI compose, clean up Docker images"
	@echo "  test-fsw      - Build and run cFS FSW unit/coverage tests"
	@echo "  test-sim      - Build and run component-simulator tests"
	@echo "  uninstall     - Remove containers, images, volumes, and networks"

list: cfg
	python3 cfg/shire-build.py list

sim: cfg
	python3 cfg/shire-build.py sim

start:
	docker compose -f $(BUILDDIR_MISSION)/shire-compose.yaml up

stop:
	@if [ -f "$(BUILDDIR_MISSION)/cli-compose.yaml" ]; then \
		docker compose -f "$(BUILDDIR_MISSION)/cli-compose.yaml" down --remove-orphans; \
	else \
		echo "Skipping missing compose file: $(BUILDDIR_MISSION)/cli-compose.yaml"; \
	fi
	@if [ -f "$(BUILDDIR_MISSION)/shire-compose.yaml" ]; then \
		docker compose -f "$(BUILDDIR_MISSION)/shire-compose.yaml" down --remove-orphans; \
	else \
		echo "Skipping missing compose file: $(BUILDDIR_MISSION)/shire-compose.yaml"; \
	fi
	@docker images -f "dangling=true" -q | xargs -r docker rmi
	@echo ""
	@echo "To cleanup Docker build cache, run: make clean-cache"
	@echo "To cleanup everything Docker, run: docker system prune -a"

test-fsw: clean-fsw cfg
	docker run --rm -v $(CURDIR):$(CURDIR) --user $(shell id -u):$(shell id -g) --sysctl fs.mqueue.msg_max=10000 --ulimit rtprio=99 --cap-add=sys_nice -w $(CURDIR)/$(FSW_DIR) $(BUILD_IMAGE) make build-test

test-sim: clean-sim container
	docker run --rm -v $(CURDIR):$(CURDIR) --user $(shell id -u):$(shell id -g) -w $(CURDIR)/simulith $(BUILD_IMAGE) make build-comp-test

uninstall: clean clean-cache
	rm -rf $(BUILD_DIR) .container.stamp
	docker ps -a --filter "name=shire-" -q | xargs -r docker rm -f
	docker images "shire-*" -q | xargs -r docker rmi -f
	docker volume ls -q --filter "name=gsw-data" | xargs -r docker volume rm -f 
	docker volume ls -q --filter "name=simulith_ipc" | xargs -r docker volume rm
	docker network ls -q --filter "name=shire-net" | xargs -r docker network rm
	docker network ls -q --filter "name=cfg_shire-net" | xargs -r docker network rm
	@echo ""
	@echo "To cleanup everything docker even unrelated to SHIRE: "
	@echo "  docker system prune -a"
	@echo ""
