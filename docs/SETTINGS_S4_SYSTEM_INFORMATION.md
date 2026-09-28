# Settings system information sources

Settings runs through the hosted guideXOS App Model on Windows. Machine fields
shown in System and About are therefore explicitly hosted-machine information:

- Computer name: Windows `GetComputerNameA`; guideXOS has no separate persistent
  computer-name owner or rename operation, so Settings displays it read-only.
- Processor model: the hosted Windows processor-name value when available.
- Logical processor count and architecture: `GetNativeSystemInfo`.
- Installed memory: `GetPhysicallyInstalledSystemMemory`; Settings shows
  unavailable if Windows cannot report installed memory rather than substituting
  a process limit or guessed value.
- Firmware: `GetFirmwareType` when the hosted Windows version provides it.
- Active display and resolution: guideXOS `DisplayConfigurationService`.
- Active desktop background: `DesktopBackgroundService`, backed by the existing
  background inventory and persisted selection. User images go through the
  existing Open Dialog and PNG validator/import transaction; in hosted builds,
  the dialog falls back to the working-directory tree when the in-memory VFS has
  no entries. A rejected image does not replace the current selection.
- Desktop theme: the shared `DesktopTheme` runtime owner. Classic and Sci Fi
  theme details and the background gallery remain available in Display Options.

This repository did not declare a guideXOS Server release version when S4 began.
Individual application manifests have application versions and minimum-version
requirements; they are not the Server release version. The QEMU
`ESP/build-identity.txt` is per-image trace evidence, not a product version.
`settings_server_identity.h` is the single Settings product-identity source and
reports “Development build” until release tooling provides a canonical release
version. The displayed build date and time come from the compiler.

Taskbar position is represented in desktop configuration and the compositor,
but no supported user-facing taskbar-position control exists in the current
Display Options UI. Settings does not expose a new taskbar control in S4.
