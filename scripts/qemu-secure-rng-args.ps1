# Canonical secure entropy device arguments for guideXOS QEMU boots.
#
# The guest kernel currently consumes the transitional PCI virtio-rng
# transport, backed by QEMU's built-in host entropy source. Keep this list
# shared between ordinary boots and validation launchers.

function Get-GxosQemuSecureRngArguments {
    return @(
        "-object", "rng-builtin,id=rng0",
        "-device", "virtio-rng-pci,rng=rng0,disable-modern=on,max-bytes=1024,period=1000"
    )
}
