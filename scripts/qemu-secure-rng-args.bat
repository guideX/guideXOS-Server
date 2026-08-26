@echo off
rem Canonical secure entropy device arguments for guideXOS QEMU boots.
rem The guest consumes transitional PCI virtio-rng backed by QEMU's
rem built-in host entropy source.
set "GXOS_QEMU_RNG_OBJECT=-object rng-builtin,id=rng0"
set "GXOS_QEMU_RNG_DEVICE=-device virtio-rng-pci,rng=rng0,disable-modern=on,max-bytes=1024,period=1000"
