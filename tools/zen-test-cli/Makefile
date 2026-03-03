.PHONY: install uninstall check-deps help

PREFIX  ?= /usr/local
BINDIR  := $(PREFIX)/bin
LIBDIR  := $(PREFIX)/lib/zen-test-cli

help:
	@echo "zen-test-cli Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  install      Install zen-test-cli to $(BINDIR)"
	@echo "  uninstall    Remove zen-test-cli from $(BINDIR)"
	@echo "  check-deps   Verify all required binaries are available"
	@echo ""
	@echo "Variables:"
	@echo "  PREFIX       Installation prefix (default: /usr/local)"

check-deps:
	@echo "Checking dependencies..."
	@command -v qemu-system-x86_64 >/dev/null 2>&1 || \
		(echo "ERROR: qemu-system-x86_64 not found. Install: sudo apt install qemu-system-x86" && exit 1)
	@command -v qemu-img >/dev/null 2>&1 || \
		(echo "ERROR: qemu-img not found. Install: sudo apt install qemu-utils" && exit 1)
	@command -v socat >/dev/null 2>&1 || \
		(echo "ERROR: socat not found. Install: sudo apt install socat" && exit 1)
	@command -v jq >/dev/null 2>&1 || \
		(echo "ERROR: jq not found. Install: sudo apt install jq" && exit 1)
	@echo ""
	@echo "Checking KVM..."
	@test -e /dev/kvm && echo "OK: /dev/kvm available" || \
		echo "WARN: /dev/kvm not found. Enable nested virtualization in WSL2 (.wslconfig: nestedVirtualization=true)"
	@echo ""
	@echo "All checks passed."

install: check-deps
	install -d $(DESTDIR)$(BINDIR)
	install -d $(DESTDIR)$(LIBDIR)
	install -m 755 zen-test-cli $(DESTDIR)$(BINDIR)/zen-test-cli
	install -m 644 lib/*.sh $(DESTDIR)$(LIBDIR)/
	@echo "Installed zen-test-cli to $(DESTDIR)$(BINDIR)/zen-test-cli"

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/zen-test-cli
	rm -rf $(DESTDIR)$(LIBDIR)
	@echo "Uninstalled zen-test-cli"
