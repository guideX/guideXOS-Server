#pragma once
// guide_web_http.h
//
// Small synchronous HTTP/1.x client for guideWeb consumers.
// Hosted builds support http:// and Schannel-backed https:// GET/POST.
// Cookies, compression, caching, and JavaScript are outside this milestone.
// POST redirects stay deliberately small: 303 becomes GET, while
// 301/302/307/308 preserve the POST method and body.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "guide_web_http_shared.h"

namespace gxos {
namespace web {

constexpr std::size_t kHttpMaxHeaderBytes = static_cast<std::size_t>(kHttpSharedMaxHeaderBytes);
constexpr std::size_t kHttpMaxBodyBytes = static_cast<std::size_t>(kHttpSharedMaxBodyBytes);
constexpr int kHttpConnectTimeoutMs = kHttpSharedConnectTimeoutMs;
constexpr int kHttpReadTimeoutMs = kHttpSharedReadTimeoutMs;
constexpr int kHttpMaxRedirects = kHttpSharedMaxRedirects;

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
	InsecureRedirectBlocked,
	UnsupportedTransferEncoding,
	UnsupportedContentEncoding,
	MalformedChunkedEncoding,
	TlsHandshakeFailed,
	TlsCertificateValidationFailed,
	TlsCertificateHostnameMismatch,
	TlsCertificateExpired,
	TlsProtocolUnsupported,
	TlsReadFailed,
	TlsWriteFailed,
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
	std::string tlsBackend;
	std::string tlsCertificateValidation;
	std::string tlsStatus;
	std::string tlsError;
	std::string tlsErrorCode;
	std::string tlsCertificateSubject;
	std::string tlsCertificateIssuer;
	std::string tlsCertificateValidFrom;
	std::string tlsCertificateValidTo;
	std::string tlsCertificateHostname;
	std::string tlsCertificateHostnameValidation;
	std::string tlsCertificateChainError;
	std::string tlsProtocol;
	std::string tlsCipherSuite;
	bool tlsEnabled = false;
	bool tlsValidated = false;
	bool tlsSmokeSelfSignedBypass = false;
	bool downgradeRedirectBlocked = false;
	std::string insecureRedirectLocation;

	bool ok() const { return error == HttpError::None; }
	std::string headerValue(const std::string& name) const;
};

ParsedHttpUrl parseHttpUrl(const std::string& url);
const char* httpErrorName(HttpError error);
HttpResponse fetchHttpUrl(const std::string& url);
HttpResponse postHttpUrl(const std::string& url, const std::string& body,
	const std::string& contentType = "application/x-www-form-urlencoded");
std::size_t httpPlainTcpByteStreamOpenCount();

} // namespace web
} // namespace gxos
