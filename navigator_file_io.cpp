// navigator_file_io.cpp
//
// Platform-specific implementation of readTextFile() for guideXOS Navigator.
// See navigator_file_io.h for the public API and mapping rules.

#include "navigator_file_io.h"
#include "logger.h"

#if !defined(GXOS_BARE_METAL)
// -------------------------------------------------------------------------
// Host / compositor path  (Windows, or any non-bare-metal build)
// Uses std::ifstream so the test harness can load files from the CWD.
// -------------------------------------------------------------------------
#include <fstream>
#include <sstream>
#if defined(_WIN32)
#include <direct.h>  // _mkdir
#endif

namespace gxos {
namespace apps {

static std::string toHostPath(const std::string& absolutePath)
{
	std::string hostPath = absolutePath;
	if (!hostPath.empty() && (hostPath[0] == '/' || hostPath[0] == '\\')) {
		hostPath = hostPath.substr(1);
	}
#if defined(_WIN32)
	for (char& c : hostPath) {
		if (c == '/') c = '\\';
	}
#endif
	return hostPath;
}

FileReadResult readTextFile(const std::string& absolutePath)
{
	FileReadResult result;

	std::string hostPath = toHostPath(absolutePath);

	Logger::write(LogLevel::Info,
		std::string("Navigator readTextFile (host): ") + hostPath);

	std::ifstream input(hostPath, std::ios::binary);
	if (!input) {
		Logger::write(LogLevel::Warn,
			std::string("Navigator readTextFile: not found: ") + hostPath);
		result.status = FileReadStatus::NotFound;
		return result;
	}

	input.seekg(0, std::ios::end);
	std::streamoff fileSize = input.tellg();
	if (fileSize < 0) {
		result.status = FileReadStatus::IoError;
		return result;
	}
	if (static_cast<uint64_t>(fileSize) > kNavigatorMaxFileBytes) {
		Logger::write(LogLevel::Warn,
			std::string("Navigator readTextFile: file too large: ") + hostPath);
		result.status = FileReadStatus::TooLarge;
		return result;
	}

	input.seekg(0, std::ios::beg);
	std::ostringstream oss;
	oss << input.rdbuf();
	if (input.fail()) {
		result.status = FileReadStatus::IoError;
		return result;
	}

	result.text   = oss.str();
	result.status = FileReadStatus::Ok;
	return result;
}

BinaryReadResult readBinaryFile(const std::string& absolutePath, uint32_t maxBytes)
{
	BinaryReadResult result;
	std::string hostPath = toHostPath(absolutePath);

	Logger::write(LogLevel::Info,
		std::string("Navigator readBinaryFile (host): ") + hostPath);

	std::ifstream input(hostPath, std::ios::binary);
	if (!input) {
		result.status = FileReadStatus::NotFound;
		return result;
	}

	input.seekg(0, std::ios::end);
	std::streamoff fileSize = input.tellg();
	if (fileSize < 0) {
		result.status = FileReadStatus::IoError;
		return result;
	}
	if (static_cast<uint64_t>(fileSize) > maxBytes) {
		result.status = FileReadStatus::TooLarge;
		return result;
	}

	input.seekg(0, std::ios::beg);
	result.bytes.resize(static_cast<size_t>(fileSize));
	if (!result.bytes.empty()) {
		input.read(reinterpret_cast<char*>(result.bytes.data()), fileSize);
		if (!input) {
			result.bytes.clear();
			result.status = FileReadStatus::IoError;
			return result;
		}
	}
	result.status = FileReadStatus::Ok;
	return result;
}

std::string imageLoaderPathForFile(const std::string& absolutePath)
{
	return toHostPath(absolutePath);
}

bool writeTextFile(const std::string& absolutePath, const std::string& text)
{
	std::string hostPath = toHostPath(absolutePath);
#if defined(_WIN32)
	// Create intermediate directories on Windows using _mkdir.
	{
		std::string dir = hostPath;
		size_t slash = dir.rfind('\\');
		if (slash != std::string::npos) {
			dir = dir.substr(0, slash);
			// mkdir every component
			std::string part;
			for (size_t i = 0; i <= dir.size(); ++i) {
				if (i == dir.size() || dir[i] == '\\') {
					if (!part.empty()) {
						_mkdir(part.c_str());
					}
				}
				if (i < dir.size()) part += dir[i];
			}
		}
	}
#endif
	std::ofstream out(hostPath, std::ios::binary | std::ios::trunc);
	if (!out) {
		Logger::write(LogLevel::Warn,
			std::string("Navigator writeTextFile: cannot open for write: ") + hostPath);
		return false;
	}
	out << text;
	return out.good();
}

} // namespace apps
} // namespace gxos

#else
// -------------------------------------------------------------------------
// Bare-metal path
// Delegates to kernel::vfs::read_file() which is initialised before any
// app is launched (see kernel/core/main.cpp).
// -------------------------------------------------------------------------
#include "kernel/vfs.h"

namespace gxos {
namespace apps {

FileReadResult readTextFile(const std::string& absolutePath)
{
	FileReadResult result;

	static char buf[kNavigatorMaxFileBytes + 1];

	int32_t bytesRead = kernel::vfs::read_file(
		absolutePath.c_str(),
		buf,
		static_cast<uint32_t>(kNavigatorMaxFileBytes));

	if (bytesRead == kernel::vfs::VFS_ERR_NOT_FOUND) {
		result.status = FileReadStatus::NotFound;
		return result;
	}
	if (bytesRead < 0) {
		result.status = FileReadStatus::IoError;
		return result;
	}
	if (static_cast<uint32_t>(bytesRead) >= kNavigatorMaxFileBytes) {
		result.status = FileReadStatus::TooLarge;
		return result;
	}

	buf[bytesRead] = '\0';
	result.text   = std::string(buf, static_cast<size_t>(bytesRead));
	result.status = FileReadStatus::Ok;
	return result;
}

BinaryReadResult readBinaryFile(const std::string& absolutePath, uint32_t maxBytes)
{
	BinaryReadResult result;
	static char buf[kNavigatorMaxImageBytes + 1];
	uint32_t cappedMax = maxBytes < kNavigatorMaxImageBytes ? maxBytes : kNavigatorMaxImageBytes;

	int32_t bytesRead = kernel::vfs::read_file(
		absolutePath.c_str(),
		buf,
		cappedMax);

	if (bytesRead == kernel::vfs::VFS_ERR_NOT_FOUND) {
		result.status = FileReadStatus::NotFound;
		return result;
	}
	if (bytesRead < 0) {
		result.status = FileReadStatus::IoError;
		return result;
	}
	if (static_cast<uint32_t>(bytesRead) >= cappedMax) {
		result.status = FileReadStatus::TooLarge;
		return result;
	}

	result.bytes.assign(reinterpret_cast<const uint8_t*>(buf),
		reinterpret_cast<const uint8_t*>(buf) + bytesRead);
	result.status = FileReadStatus::Ok;
	return result;
}

std::string imageLoaderPathForFile(const std::string& absolutePath)
{
	return absolutePath;
}

bool writeTextFile(const std::string& absolutePath, const std::string& text)
{
	int32_t ret = kernel::vfs::write_file(
		absolutePath.c_str(),
		text.c_str(),
		static_cast<uint32_t>(text.size()));
	return (ret >= 0);
}

} // namespace apps
} // namespace gxos

#endif // GXOS_BARE_METAL
