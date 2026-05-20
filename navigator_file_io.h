#pragma once
// navigator_file_io.h
//
// Platform-abstracted synchronous text-file reader used exclusively by
// guideXOS Navigator.
//
// Host / compositor mode  (!GXOS_BARE_METAL):
//   Uses std::ifstream so the Windows test harness can load files from the
//   working directory alongside the binary.
//
// Bare-metal mode  (GXOS_BARE_METAL):
//   Uses kernel::vfs::read_file() which is already initialised and mounted
//   before any app is launched.
//
// Callers only see readTextFile() and FileReadResult; the platform detail
// is hidden in the .cpp translation unit.

#include <string>
#include <cstdint>

namespace gxos {
namespace apps {

// Maximum bytes Navigator will load from a single file.
// Keeps stack/heap pressure predictable on bare-metal targets.
static constexpr uint32_t kNavigatorMaxFileBytes = 64u * 1024u;

enum class FileReadStatus : uint8_t {
	Ok          = 0,
	NotFound    = 1,  // file does not exist
	TooLarge    = 2,  // file exceeds kNavigatorMaxFileBytes
	IoError     = 3,  // read failed mid-stream
};

struct FileReadResult {
	FileReadStatus status = FileReadStatus::NotFound;
	std::string    text;   // populated on Ok
};

// Read the entire contents of |absolutePath| as UTF-8 / Latin-1 text.
//
// |absolutePath| must be an absolute POSIX-style path (e.g. /docs/index.html).
//
// On the host the leading '/' is stripped and the path is resolved relative to
// the process working directory so that running from D:\dev\guideXOSServer\
// finds docs\index.html naturally.
FileReadResult readTextFile(const std::string& absolutePath);

// Write |text| to |absolutePath|, creating the file (and any missing
// intermediate directories) if necessary.  Returns true on success.
// On the host the leading '/' is stripped as with readTextFile().
bool writeTextFile(const std::string& absolutePath, const std::string& text);

} // namespace apps
} // namespace gxos
