#
# make
# make all     -- build everything
#
# make test    -- build and run the unit tests
#
# make install -- install blissc to ~/.local/bin (or /usr/local/bin)
#
# make format  -- reformat all C sources with clang-format
#
# make clean   -- remove build files
#
# To reconfigure for Debug build:
#   make debug; make
#
.PHONY: all test install format clean debug

all:    build
	$(MAKE) -Cbuild $@

test:   build
	$(MAKE) -Cbuild all
	ctest --test-dir build --progress --output-on-failure

install: all
	@prefix=$$( [ -d "$$HOME/.local" ] && echo "$$HOME/.local" || echo /usr/local ); \
	echo "Installing to $$prefix"; \
	cmake --install build --prefix "$$prefix"

format:
	git ls-files '*.c' '*.h' ':!src/linenoise' | xargs clang-format -i

clean:
	rm -rf build

build:
	cmake -B$@ -DCMAKE_BUILD_TYPE=RelWithDebInfo

debug:
	cmake -Bbuild -DCMAKE_BUILD_TYPE=Debug
