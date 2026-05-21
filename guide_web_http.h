#pragma once
// guide_web_http.h
//
// Small synchronous HTTP/1.x client for guideWeb consumers.
// This module intentionally supports only plain http:// GET for text
// documents.  TLS, cookies, compression, caching, and
// JavaScript are outside this milestone.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gxos {
namespace web {

constexpr std::size_t kHttpMaxHeaderBytes = 32u * 1024u;
constexpr std::size_t kHttpMaxBodyBytes = 256u * 1024u;
constexpr int kHttpConnectTimeoutMs = 5000;
constexpr int kHttpReadTimeoutMs = 5000;
constexpr int kHttpMaxRedirects = 5;

struct ParsedHttpUrl {
	std::string scheme;
	std::string host;
	uint16_t    port = 80;
	std::string path = "/";
	std::string query;
	bool        valid = false;
	std::string error;

	std::string requestTarget() const;
	std::string origin() const;
};

struct HttpHeader {
	std::string name;
	std::string value;
};

enum class HttpError {
	None = 0,
	InvalidUrl,
	UnsupportedScheme,
	NetworkUnavailable,
	ResolveFailed,
	ConnectFailed,
	Timeout,
	SendFailed,
	ReceiveFailed,
	HeaderTooLarge,
	BodyTooLarge,
	MalformedResponse,
	RedirectLimitExceeded,
	UnsupportedTransferEncoding,
	UnsupportedContentEncoding,
	MalformedChunkedEncoding,
};

struct HttpResponse {
	std::string requestedUrl;
	std::string finalUrl;
	int statusCode = 0;
	std::string reasonPhrase;
	std::vector<HttpHeader> headers;
	std::string body;
	std::string contentType;
	std::string transferEncoding;
	std::string contentEncoding;
	int redirectCount = 0;
	std::vector<std::string> redirectChain;
	HttpError error = HttpError::None;
	std::string errorMessage;

	bool ok() const { return error == HttpError::None; }
	std::string headerValue(const std::string& name) const;
};

ParsedHttpUrl parseHttpUrl(const std::string& url);
const char* httpErrorName(HttpError error);
HttpResponse fetchHttpUrl(const std::string& url);

} // namespace web
} // namespace gxos
