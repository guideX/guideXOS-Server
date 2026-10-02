#pragma once

#include "app_activation.h"

#include <string>
#include <vector>

namespace gxos {
namespace apps {

constexpr const char* kNavigatorCanonicalAppId = "guidexos.navigator";

inline bool NavigatorIsRemoteDocumentUrl(const std::string& url)
{
	return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
}

inline bool NavigatorIsLocalFileUrl(const std::string& url)
{
	return url.rfind("file:///", 0) == 0;
}

inline bool NavigatorIsLocalHtmlPath(const std::string& path)
{
	if (!IsValidDocumentActivationPath(path) || path[0] != '/') return false;
	std::vector<std::string> components;
	size_t start = 1;
	while (start <= path.size()) {
		size_t end = path.find('/', start);
		if (end == std::string::npos) end = path.size();
		const std::string component = path.substr(start, end - start);
		if (component == "..") {
			if (components.empty()) return false;
			components.pop_back();
		} else if (!component.empty() && component != ".") {
			if (component.find('\\') != std::string::npos || component.find(':') != std::string::npos)
				return false;
			components.push_back(component);
		}
		if (end == path.size()) break;
		start = end + 1;
	}
	if (components.empty()) return false;
	std::string name = components.back();
	for (char& ch : name) {
		if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
	}
	return name.size() >= 5 && (name.compare(name.size() - 5, 5, ".html") == 0 ||
		(name.size() >= 4 && name.compare(name.size() - 4, 4, ".htm") == 0));
}

inline bool NavigatorLocalHtmlPathToUrl(const std::string& path, std::string& url)
{
	url.clear();
	if (!NavigatorIsLocalHtmlPath(path)) return false;

	std::vector<std::string> components;
	size_t start = 1;
	while (start <= path.size()) {
		size_t end = path.find('/', start);
		if (end == std::string::npos) end = path.size();
		const std::string component = path.substr(start, end - start);
		if (component == "..") {
			if (components.empty()) return false;
			components.pop_back();
		} else if (!component.empty() && component != ".") {
			components.push_back(component);
		}
		if (end == path.size()) break;
		start = end + 1;
	}
	if (components.empty()) return false;

	static const char hex[] = "0123456789ABCDEF";
	url = "file:///";
	for (size_t componentIndex = 0; componentIndex < components.size(); ++componentIndex) {
		if (componentIndex != 0) url += "/";
		for (unsigned char ch : components[componentIndex]) {
			const bool unreserved = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
				(ch >= '0' && ch <= '9') || ch == '-' || ch == '.' || ch == '_' || ch == '~';
			if (unreserved) url += static_cast<char>(ch);
			else {
				url += '%';
				url += hex[ch >> 4];
				url += hex[ch & 0x0Fu];
			}
		}
	}
	return true;
}

inline bool NavigatorLocalFileUrlToPath(const std::string& url, std::string& path)
{
	path.clear();
	if (!NavigatorIsLocalFileUrl(url)) return false;

	auto hexValue = [](char ch) -> int {
		if (ch >= '0' && ch <= '9') return ch - '0';
		if (ch >= 'a' && ch <= 'f') return 10 + ch - 'a';
		if (ch >= 'A' && ch <= 'F') return 10 + ch - 'A';
		return -1;
	};
	path = "/";
	for (size_t i = 8; i < url.size(); ++i) {
		const unsigned char ch = static_cast<unsigned char>(url[i]);
		if (ch == '%') {
			if (i + 2 >= url.size()) { path.clear(); return false; }
			const int high = hexValue(url[i + 1]);
			const int low = hexValue(url[i + 2]);
			if (high < 0 || low < 0) { path.clear(); return false; }
			const unsigned char decoded = static_cast<unsigned char>((high << 4) | low);
			if (decoded == 0 || decoded < 0x20u || decoded == 0x7fu || decoded == '\\') { path.clear(); return false; }
			path += static_cast<char>(decoded);
			i += 2;
		} else {
			if (ch == 0 || ch < 0x20u || ch == 0x7fu || ch == '\\') { path.clear(); return false; }
			path += static_cast<char>(ch);
		}
	}
	return path.size() > 1;
}

inline bool NavigatorMayFollowLocalLink(const std::string& currentUrl, const std::string& targetUrl)
{
	return !(NavigatorIsRemoteDocumentUrl(currentUrl) && NavigatorIsLocalFileUrl(targetUrl));
}

inline bool NavigatorMayLoadLocalResource(const std::string& currentUrl, const std::string& resourceUrl)
{
	return !(NavigatorIsRemoteDocumentUrl(currentUrl) && NavigatorIsLocalFileUrl(resourceUrl));
}

} // namespace apps
} // namespace gxos
