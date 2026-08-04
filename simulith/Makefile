# Makefile for SHIRE Simulith development
.PHONY: build build-comp-test clean debug director server stop test test-comp

export BUILDDIR ?= $(CURDIR)/build
export TOPDIR ?= $(CURDIR)/..

export BUILD_IMAGE ?= ghcr.io/voyagertechnologies-public/shire-base:latest
export CONTAINER_NAME ?= shire-simulith
export RUNTIME_DIRECTOR_NAME ?= shire-director-$(MISSION)
export RUNTIME_SERVER_NAME ?= shire-server-$(MISSION)
export SPACECRAFT ?= latest
export MISSION ?= default

# Determine number of parallel jobs to avoid maxing out low-power systems (Raspberry Pi etc.).
# Use `nproc - 1` but ensure at least 1 job.
NPROC := $(shell nproc 2>/dev/null || echo 1)
JOBS := $(shell if [ $(NPROC) -le 1 ]; then echo 1; else expr $(NPROC) - 1; fi)

# Commands
build:
	docker run --rm -v $(TOPDIR):$(TOPDIR) --user $(shell id -u):$(shell id -g) --name $(CONTAINER_NAME) -w $(CURDIR) -e BUILDDIR=$(BUILDDIR) $(BUILD_IMAGE) make -j$(JOBS) build-sim

build-director: 
	mkdir -p $(BUILDDIR)
	cd $(BUILDDIR) && cmake $(CURDIR)
	$(MAKE) --no-print-directory -C $(BUILDDIR) simulith_director_standalone

build-server: 
	mkdir -p $(BUILDDIR)
	cd $(BUILDDIR) && cmake $(CURDIR)
	$(MAKE) --no-print-directory -C $(BUILDDIR) simulith_server_standalone

build-sim:
	$(MAKE) build-director
	$(MAKE) build-server

build-test:
	mkdir -p $(BUILDDIR)
	cd $(BUILDDIR) && cmake $(CURDIR) -DBUILD_SIMULITH_TESTS=ON -DENABLE_UNIT_TESTS=true
	$(MAKE) --no-print-directory -C $(BUILDDIR)
	cd $(BUILDDIR) && ctest --output-on-failure -O ctest.log
	lcov -c --directory . --output-file $(BUILDDIR)/coverage.info
	lcov --remove $(BUILDDIR)/coverage.info '*/test/*' '*/tests/*' --output-file $(BUILDDIR)/coverage.filtered.info
	genhtml $(BUILDDIR)/coverage.filtered.info --output-directory $(BUILDDIR)/report
	@echo ""
	@echo "Review coverage report: "
	@echo "  firefox $(BUILDDIR)/report/index.html"
	@echo ""

copy-sims:
	@if [ ! -d "$(BUILDDIR)" ]; then \
		echo "Error: Build directory $(BUILDDIR) does not exist. Please run 'make build' first."; \
		exit 1; \
	fi
	mkdir -p $(BUILDDIR)/components
	@# Check if running from top-level with unified build structure
	@if [ -n "$(BUILDDIR_COMP)" ]; then \
		for comp_dir in $(TOPDIR)/comp/*/sim ; do \
			if [ -d "$$comp_dir" ]; then \
				comp_name=$$(basename $$(dirname "$$comp_dir")); \
				comp_build_dir="$(BUILDDIR_COMP)/$$comp_name/sim"; \
				if [ -d "$$comp_build_dir" ]; then \
					cp $$comp_build_dir/*.so $(BUILDDIR)/components/ 2>/dev/null || true; \
				fi; \
			fi; \
		done; \
	else \
		for dir in $(TOPDIR)/comp/*/sim/build ; do \
			if [ -d "$$dir" ]; then \
				cp $$dir/*.so $(BUILDDIR)/components/ 2>/dev/null || true; \
			fi; \
		done; \
	fi
	cp -r $(TOPDIR)/42/InOut $(BUILDDIR)/ 2>/dev/null || true
	cp -r $(TOPDIR)/42/Model $(BUILDDIR)/ 2>/dev/null || true

clean:
	rm -rf $(BUILDDIR)

debug:
	docker run --rm -it -v $(TOPDIR):$(TOPDIR) --user $(shell id -u):$(shell id -g) --name $(CONTAINER_NAME) -w $(CURDIR) $(BUILD_IMAGE) /bin/bash

director: copy-sims
	@if [ ! -d "$(BUILDDIR)" ]; then \
		echo "Error: Build directory $(BUILDDIR) does not exist. Please run 'make build' first."; \
		exit 1; \
	fi
	@# Prepare Docker build context
	@if [ "$(BUILDDIR)" != "$(CURDIR)/build" ]; then \
		rm -rf build; \
		mkdir -p build; \
		cp -L $(BUILDDIR)/* build/ 2>/dev/null || true; \
		cp -rL $(BUILDDIR)/components build/ 2>/dev/null || true; \
		cp -rL $(BUILDDIR)/InOut build/ 2>/dev/null || true; \
		cp -rL $(BUILDDIR)/Model build/ 2>/dev/null || true; \
	fi
	docker build -t $(RUNTIME_DIRECTOR_NAME):$(SPACECRAFT) -f docker/Dockerfile.director .

server:
	@if [ ! -d "$(BUILDDIR)" ]; then \
		echo "Error: Build directory $(BUILDDIR) does not exist. Please run 'make build' first."; \
		exit 1; \
	fi
	@# Prepare Docker build context
	@if [ "$(BUILDDIR)" != "$(CURDIR)/build" ]; then \
		rm -rf build; \
		mkdir -p build; \
		cp -L $(BUILDDIR)/* build/ 2>/dev/null || true; \
		cp -rL $(BUILDDIR)/components build/ 2>/dev/null || true; \
		cp -rL $(BUILDDIR)/InOut build/ 2>/dev/null || true; \
		cp -rL $(BUILDDIR)/Model build/ 2>/dev/null || true; \
	fi
	docker build -t $(RUNTIME_SERVER_NAME):$(SPACECRAFT) -f docker/Dockerfile.server .

stop:
	docker ps --filter name=shire-* | xargs docker stop

test:
	docker run --rm -v $(TOPDIR):$(TOPDIR) --user $(shell id -u):$(shell id -g) --name $(CONTAINER_NAME) -w $(CURDIR) $(BUILD_IMAGE) make -j$(JOBS) build-test

# Discover testable components by reading build/build.yaml.
# Falls back to "demo" if build.yaml is absent.
COMP_TESTS ?= $(shell python3 -c "import yaml,os;f='$(TOPDIR)/build/build.yaml';cfg=yaml.safe_load(open(f)) if os.path.exists(f) else {};sc=cfg.get('spacecraft_cfg',{});comps=(sc if 'components' in sc else cfg.get('mission_cfg',{})).get('components',[]);print(' '.join(c['name'] for c in comps if 'name' in c and os.path.isdir('$(TOPDIR)/comp/'+c['name']+'/test-sim')))" 2>/dev/null || echo "demo eps radio")

# Build and run the component-simulator tests for each entry in COMP_TESTS.
# Depends on libsimulith.so in $(CURDIR)/build, which build-sim produces.
build-comp-test:
	$(MAKE) --no-print-directory build-sim
	@for comp in $(COMP_TESTS); do \
	    echo "===== build-comp-test: $$comp ====="; \
	    rm -rf $(TOPDIR)/comp/$$comp/sim/build; \
	    mkdir -p $(TOPDIR)/comp/$$comp/sim/build; \
	    cd $(TOPDIR)/comp/$$comp/sim/build && cmake .. -DBUILD_SIM_TESTS=ON -DENABLE_UNIT_TESTS=true || exit 1; \
	    $(MAKE) --no-print-directory -C $(TOPDIR)/comp/$$comp/sim/build || exit 1; \
	    cd $(TOPDIR)/comp/$$comp/sim/build && ctest --output-on-failure -O ctest.log || exit 1; \
	    lcov -c --directory $(TOPDIR)/comp/$$comp/sim/build --output-file $(TOPDIR)/comp/$$comp/sim/build/coverage.info; \
	    lcov --remove $(TOPDIR)/comp/$$comp/sim/build/coverage.info '*/test/*' '*/tests/*' '*/test-sim/*' '*/test-fsw/*' '*/hwlib/*' '*/shared/*' --output-file $(TOPDIR)/comp/$$comp/sim/build/coverage.filtered.info; \
	done
	@# Merge all per-component filtered traces into one combined report.
	@COMBINED_DIR=$(TOPDIR)/build/sim-coverage; \
	mkdir -p $$COMBINED_DIR; \
	ADD_FLAGS=""; \
	for comp in $(COMP_TESTS); do \
	    info=$(TOPDIR)/comp/$$comp/sim/build/coverage.filtered.info; \
	    if [ -f "$$info" ]; then ADD_FLAGS="$$ADD_FLAGS --add-tracefile $$info"; fi; \
	done; \
	lcov $$ADD_FLAGS --output-file $$COMBINED_DIR/coverage.info; \
	genhtml $$COMBINED_DIR/coverage.info --output-directory $$COMBINED_DIR/report; \
	echo ""; \
	echo "Combined simulator coverage report:"; \
	echo "  firefox $$COMBINED_DIR/report/index.html"; \
	echo ""

test-comp:
	docker run --rm -v $(TOPDIR):$(TOPDIR) --user $(shell id -u):$(shell id -g) --name $(CONTAINER_NAME) -w $(CURDIR) $(BUILD_IMAGE) make -j$(JOBS) build-comp-test
