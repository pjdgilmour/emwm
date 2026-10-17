all:
	@if [ -e src/Makefile ]; then \
		$(MAKE) -C src $(MAKEFLAGS); \
	else \
		if [ -e mf/Makefile.$$(uname) ]; then \
			ln -s ../mf/Makefile.$$(uname) src/Makefile && \
			$(MAKE) -C src $(MAKEFLAGS); \
		else \
			echo "Run: make <target>" && \
			echo "Available targets are:" && \
			ls mf/ | sed 's/Makefile\.//g'; \
		fi \
	fi

.PHONY: clean install distclean test test-argb test-tiling

test: all
	./tests/run-x11.sh
	FRAME_STYLE=slab DECOUPLED_TITLE=1 ./tests/run-x11.sh

test-argb: all
	ARGB_CLIENTS=1 ./tests/run-x11.sh
	ARGB_CLIENTS=1 FRAME_STYLE=slab DECOUPLED_TITLE=1 MATTE_WIDTH=4 XFT_TITLE=1 ./tests/run-x11.sh

test-tiling: all
	./tests/run-tiling.sh
	FRAME_STYLE=slab MATTE_WIDTH=4 ./tests/run-tiling.sh
	TILE_BINDINGS=NoTiling NO_TILE_BINDINGS=1 ./tests/run-tiling.sh
	TILE_BINDINGS=DefaultKeyBindings TILE_CONFIG="$(CURDIR)/src/system.emwmrc" DEFAULT_TILE_BINDINGS=1 ./tests/run-tiling.sh
	TILE_BINDINGS=_MwmKeyBindings_ TILE_CONFIG=/nonexistent-emwm-tiling-config DEFAULT_TILE_BINDINGS=1 ./tests/run-tiling.sh

install:
	$(MAKE) -C src $(MAKEFLAGS) install

uninstall:
	$(MAKE) -C src $(MAKEFLAGS) uninstall

clean:
	$(MAKE) -C src $(MAKEFLAGS) clean

distclean:
	-$(MAKE) -C src $(MAKEFLAGS) clean
	-rm src/Makefile
	
.DEFAULT:
	@if [ -e src/Makefile ]; then rm src/Makefile; fi
	@if ! [ -f mf/Makefile.$@ ]; then \
		echo "Invalid target name: $@" && exit 1; fi
	ln -s ../mf/Makefile.$@ src/Makefile
	$(MAKE) -C src $(MAKEFLAGS)
