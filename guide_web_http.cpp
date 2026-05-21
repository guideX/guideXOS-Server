#include "guide_web_http.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#endif

namespace gxos {
namespace web {
namespace {

static std::string toLowerAscii(std::string value)
{
	for (char& ch : value) {
		ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
	}
	return value;
}

static std::string trimAscii(const std::string& value)
{
	size_t begin = 0;
	while (begin < value.size() &&
		std::isspace(static_cast<unsigned char>(value[begin]))) {
		++begin;
	}
	size_t end = value.size();
	while (end > begin &&
		std::isspace(static_cast<unsigned char>(value[end - 1]))) {
		--end;
	}
	return value.substr(begin, end - begin);
}

static bool parsePort(const std::string& text, uint16_t& outPort)
{
	if (text.empty()) return false;
	unsigned long port = 0;
	for (char ch : text) {
		if (ch < '0' || ch > '9') return false;
		port = port * 10u + static_cast<unsigned long>(ch - '0');
		if (port > 65535u) return false;
	}
	if (port == 0u) return false;
	outPort = static_cast<uint16_t>(port);
	return true;
}

static void setError(HttpResponse& response, HttpError error, const std::string& message)
{
	response.error = error;
	response.errorMessage = message;
}

static std::string firstContentTypeToken(std::string value)
{
	size_t semi = value.find(';');
	if (semi != std::string::npos) value = value.substr(0, semi);
	return toLowerAscii(trimAscii(value));
}

static bool parseHttpResponse(const std::string& raw, HttpResponse& response)
{
	size_t headerEnd = raw.find("\r\n\r\n");
	size_t delimiterLen = 4;
	if (headerEnd == std::string::npos) {
		headerEnd = raw.find("\n\n");
		delimiterLen = 2;
	}
	if (headerEnd == std::string::npos) {
		setError(response, HttpError::MalformedResponse, "HTTP response did not include a complete header block.");
		return false;
	}

	std::string headerText = raw.substr(0, headerEnd);
	response.body = raw.substr(headerEnd + delimiterLen);

	std::istringstream stream(headerText);
	std::string line;
	if (!std::getline(stream, line)) {
		setError(response, HttpError::MalformedResponse, "HTTP response status line was missing.");
		return false;
	}
	if (!line.empty() && line.back() == '\r') line.pop_back();

	std::istringstream statusLine(line);
	std::string version;
	statusLine >> version >> response.statusCode;
	std::getline(statusLine, response.reasonPhrase);
	response.reasonPhrase = trimAscii(response.reasonPhrase);
	if (version.rfind("HTTP/", 0) != 0 || response.statusCode <= 0) {
		setError(response, HttpError::MalformedResponse, "HTTP response status line was malformed.");
		return false;
	}

	while (std::getline(stream, line)) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		if (line.empty()) continue;
		size_t colon = line.find(':');
		if (colon == std::string::npos) continue;
		HttpHeader header;
		header.name = trimAscii(line.substr(0, colon));
		header.value = trimAscii(line.substr(colon + 1));
		if (!header.name.empty()) response.headers.push_back(header);
	}

	response.contentType = firstContentTypeToken(response.headerValue("Content-Type"));
	return true;
}

#if defined(_WIN32)
class WinsockSession {
public:
	WinsockSession()
	{
		WSADATA data;
		m_ok = (WSAStartup(MAKEWORD(2, 2), &data) == 0);
	}
	~WinsockSession()
	{
		if (m_ok) WSACleanup();
	}
	bool ok() const { return m_ok; }
private:
	bool m_ok = false;
};

static bool ensureWinsock()
{
	static WinsockSession session;
	return session.ok();
}

static bool waitForSocket(SOCKET sock, bool writeReady, int timeoutMs)
{
	fd_set fds;
	FD_ZERO(&fds);
	FD_SET(sock, &fds);
	timeval tv;
	tv.tv_sec = timeoutMs / 1000;
	tv.tv_usec = (timeoutMs % 1000) * 1000;
	int result = select(0, writeReady ? nullptr : &fds, writeReady ? &fds : nullptr, nullptr, &tv);
	return result > 0 && FD_ISSET(sock, &fds);
}
#endif

} // anonymous namespace

std::string ParsedHttpUrl::requestTarget() const
{
	return query.empty() ? path : path + "?" + query;
}

std::string ParsedHttpUrl::origin() const
{
	std::ostringstream out;
	out << scheme << "://" << host;
	if (!(scheme == "http" && port == 80)) {
		out << ":" << port;
	}
	return out.str();
}

ParsedHttpUrl parseHttpUrl(const std::string& url)
{
	ParsedHttpUrl parsed;
	size_t schemeEnd = url.find("://");
	if (schemeEnd == std::string::npos) {
		parsed.error = "URL is missing a scheme.";
		return parsed;
	}
	parsed.scheme = toLowerAscii(url.substr(0, schemeEnd));
	if (parsed.scheme != "http") {
		parsed.error = "Only http:// URLs are supported by this HTTP client.";
		return parsed;
	}

	size_t authorityStart = schemeEnd + 3;
	size_t pathStart = url.find_first_of("/?#", authorityStart);
	std::string authority = pathStart == std::string::npos
		? url.substr(authorityStart)
		: url.substr(authorityStart, pathStart - authorityStart);
	if (authority.empty()) {
		parsed.error = "URL host is empty.";
		return parsed;
	}
	if (authority.find('@') != std::string::npos) {
		parsed.error = "User-info in URLs is not supported.";
		return parsed;
	}
	if (!authority.empty() && authority.front() == '[') {
		parsed.error = "IPv6 host syntax is not supported yet.";
		return parsed;
	}

	size_t colon = authority.rfind(':');
	if (colon != std::string::npos) {
		parsed.host = authority.substr(0, colon);
		if (!parsePort(authority.substr(colon + 1), parsed.port)) {
			parsed.error = "URL port is invalid.";
			return parsed;
		}
	} else {
		parsed.host = authority;
		parsed.port = 80;
	}
	if (parsed.host.empty()) {
		parsed.error = "URL host is empty.";
		return parsed;
	}

	if (pathStart == std::string::npos) {
		parsed.path = "/";
	} else {
		size_t queryStart = url.find('?', pathStart);
		size_t fragmentStart = url.find('#', pathStart);
		size_t pathEnd = std::min(queryStart == std::string::npos ? url.size() : queryStart,
			fragmentStart == std::string::npos ? url.size() : fragmentStart);
		if (url[pathStart] == '/') {
			parsed.path = url.substr(pathStart, pathEnd - pathStart);
		} else {
			parsed.path = "/";
		}
		if (parsed.path.empty()) parsed.path = "/";
		if (queryStart != std::string::npos &&
			(fragmentStart == std::string::npos || queryStart < fragmentStart)) {
			size_t queryEnd = fragmentStart == std::string::npos ? url.size() : fragmentStart;
			parsed.query = url.substr(queryStart + 1, queryEnd - queryStart - 1);
		}
	}

	parsed.valid = true;
	return parsed;
}

const char* httpErrorName(HttpError error)
{
	switch (error) {
	case HttpError::None: return "None";
	case HttpError::InvalidUrl: return "InvalidUrl";
	case HttpError::UnsupportedScheme: return "UnsupportedScheme";
	case HttpError::NetworkUnavailable: return "NetworkUnavailable";
	case HttpError::ResolveFailed: return "ResolveFailed";
	case HttpError::ConnectFailed: return "ConnectFailed";
	case HttpError::Timeout: return "Timeout";
	case HttpError::SendFailed: return "SendFailed";
	case HttpError::ReceiveFailed: return "ReceiveFailed";
	case HttpError::HeaderTooLarge: return "HeaderTooLarge";
	case HttpError::BodyTooLarge: return "BodyTooLarge";
	case HttpError::MalformedResponse: return "MalformedResponse";
	}
	return "Unknown";
}

std::string HttpResponse::headerValue(const std::string& name) const
{
	const std::string needle = toLowerAscii(name);
	for (const HttpHeader& header : headers) {
		if (toLowerAscii(header.name) == needle) return header.value;
	}
	return "";
}

HttpResponse fetchHttpUrl(const std::string& url)
{
	HttpResponse response;
	ParsedHttpUrl parsed = parseHttpUrl(url);
	if (!parsed.valid) {
		setError(response,
			parsed.scheme.empty() || parsed.scheme == "http" ? HttpError::InvalidUrl : HttpError::UnsupportedScheme,
			parsed.error);
		return response;
	}

#if defined(_WIN32)
	if (!ensureWinsock()) {
		setError(response, HttpError::NetworkUnavailable, "Winsock could not be initialized.");
		return response;
	}

	addrinfo hints = {};
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	addrinfo* addresses = nullptr;
	std::string portText = std::to_string(parsed.port);
	int gai = getaddrinfo(parsed.host.c_str(), portText.c_str(), &hints, &addresses);
	if (gai != 0 || !addresses) {
		setError(response, HttpError::ResolveFailed, "Could not resolve host: " + parsed.host);
		return response;
	}

	SOCKET sock = INVALID_SOCKET;
	HttpError connectError = HttpError::ConnectFailed;
	std::string connectMessage = "Could not connect to " + parsed.host + ":" + portText;
	for (addrinfo* addr = addresses; addr; addr = addr->ai_next) {
		sock = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
		if (sock == INVALID_SOCKET) continue;

		u_long nonBlocking = 1;
		ioctlsocket(sock, FIONBIO, &nonBlocking);
		int result = connect(sock, addr->ai_addr, static_cast<int>(addr->ai_addrlen));
		if (result == 0 || WSAGetLastError() == WSAEWOULDBLOCK) {
			if (result == 0 || waitForSocket(sock, true, kHttpConnectTimeoutMs)) {
				int socketError = 0;
				int socketErrorLen = sizeof(socketError);
				getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&socketError), &socketErrorLen);
				if (socketError == 0) {
					nonBlocking = 0;
					ioctlsocket(sock, FIONBIO, &nonBlocking);
					DWORD timeout = kHttpReadTimeoutMs;
					setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
					setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
					break;
				}
			} else {
				connectError = HttpError::Timeout;
				connectMessage = "Connection timed out.";
			}
		}
		closesocket(sock);
		sock = INVALID_SOCKET;
	}
	freeaddrinfo(addresses);

	if (sock == INVALID_SOCKET) {
		setError(response, connectError, connectMessage);
		return response;
	}

	std::ostringstream request;
	request << "GET " << parsed.requestTarget() << " HTTP/1.0\r\n"
		<< "Host: " << parsed.host;
	if (parsed.port != 80) request << ":" << parsed.port;
	request << "\r\n"
		<< "User-Agent: guideXOS-Navigator/0.1\r\n"
		<< "Connection: close\r\n"
		<< "\r\n";
	const std::string requestText = request.str();
	size_t sentTotal = 0;
	while (sentTotal < requestText.size()) {
		int sent = send(sock, requestText.data() + sentTotal,
			static_cast<int>(requestText.size() - sentTotal), 0);
		if (sent <= 0) {
			closesocket(sock);
			setError(response, HttpError::SendFailed, "Failed while sending HTTP request.");
			return response;
		}
		sentTotal += static_cast<size_t>(sent);
	}

	std::string raw;
	raw.reserve(16u * 1024u);
	bool sawHeaderEnd = false;
	size_t headerBytes = 0;
	char buffer[4096];
	for (;;) {
		int received = recv(sock, buffer, sizeof(buffer), 0);
		if (received == 0) break;
		if (received < 0) {
			int error = WSAGetLastError();
			closesocket(sock);
			setError(response,
				error == WSAETIMEDOUT ? HttpError::Timeout : HttpError::ReceiveFailed,
				error == WSAETIMEDOUT ? "Timed out while reading HTTP response." : "Failed while reading HTTP response.");
			return response;
		}
		raw.append(buffer, buffer + received);
		if (!sawHeaderEnd) {
			size_t end = raw.find("\r\n\r\n");
			size_t delimiterLen = 4;
			if (end == std::string::npos) {
				end = raw.find("\n\n");
				delimiterLen = 2;
			}
			if (end != std::string::npos) {
				sawHeaderEnd = true;
				headerBytes = end + delimiterLen;
			} else if (raw.size() > kHttpMaxHeaderBytes) {
				closesocket(sock);
				setError(response, HttpError::HeaderTooLarge, "HTTP response headers exceeded the safety limit.");
				return response;
			}
		}
		if (sawHeaderEnd && raw.size() > headerBytes + kHttpMaxBodyBytes) {
			closesocket(sock);
			setError(response, HttpError::BodyTooLarge, "HTTP response body exceeded the safety limit.");
			return response;
		}
	}
	closesocket(sock);

	parseHttpResponse(raw, response);
	return response;
#else
	setError(response, HttpError::NetworkUnavailable, "HTTP networking is unavailable in this runtime.");
	return response;
#endif
}

} // namespace web
} // namespace gxos
