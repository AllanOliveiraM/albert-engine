.DEFAULT_GOAL := render-build
.PHONY: setup bootstrap build run test dist clean render-build render-run render-test render-dist render-clean assets assets-body

JOBS ?= 2
BODY ?= mercury
ifeq ($(OS),Windows_NT)
SETUP := powershell -NoProfile -ExecutionPolicy Bypass -File scripts/setup-windows.ps1 -Task setup
else
PLATFORM := linux
EXECUTABLE := ./build/linux/bin/AlbertEngine
SETUP := bash scripts/setup-linux.sh
endif

# GNU Make works on both systems. Windows also has a make.cmd wrapper.
setup:
	$(SETUP)

bootstrap: setup
	$(MAKE) render-dist JOBS=$(JOBS)

build: render-build
run: render-run
test: render-test
dist: render-dist
clean: render-clean

ifeq ($(OS),Windows_NT)
render-build render-run render-test render-dist render-clean:
	powershell -NoProfile -ExecutionPolicy Bypass -File scripts/setup-windows.ps1 -Task $@ -Jobs $(JOBS)
else
render-build:
	python3 scripts/build-linux.py --jobs $(JOBS)

render-run:
	python3 scripts/build-linux.py --jobs $(JOBS) --target render
	"$(EXECUTABLE)"

render-test: render-build
	ctest --preset $(PLATFORM)

render-dist: assets render-test
	cmake -E rm -rf "dist/$(PLATFORM)"
	cmake --install "build/$(PLATFORM)" --prefix "$(CURDIR)/dist/$(PLATFORM)" --component Runtime --strip

render-clean:
	cmake -E rm -rf "build/$(PLATFORM)"
endif

# Optional high-resolution library for hosting; requires Docker.
assets:
ifeq ($(OS),Windows_NT)
	powershell -NoProfile -ExecutionPolicy Bypass -File scripts/setup-windows.ps1 -Task assets
else
	bash scripts/prepare-assets.sh
endif

# Regenerate one additional body's archive; existing image parts stay intact.
assets-body:
ifeq ($(OS),Windows_NT)
	powershell -NoProfile -ExecutionPolicy Bypass -File scripts/setup-windows.ps1 -Task assets-body -Body "$(BODY)"
else
	bash scripts/prepare-assets.sh --body "$(BODY)"
endif
