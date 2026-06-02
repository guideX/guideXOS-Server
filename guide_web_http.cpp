#include "guide_web_http.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <sstream>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#define SECURITY_WIN32
#include <security.h>
#include <schannel.h>
#include <wincrypt.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "secur32.lib")
#pragma comment(lib, "crypt32.lib")
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

static bool isSupportedHttpScheme(const std::string& scheme)
{
	return scheme == "http" || scheme == "https";
}

static void setError(HttpResponse& response, HttpError error, const std::string& message)
{
	response.error = error;
	response.errorMessage = message;
}

static std::string firstContentTypeToken(std::string value)
{
	char normalized[96];
	httpSharedNormalizeContentType(value.data(), value.data() + value.size(),
		normalized, static_cast<int>(sizeof(normalized)));
	return normalized;
}

static bool hasHeaderToken(const std::string& value, const std::string& token)
{
	return httpSharedHeaderHasToken(value.c_str(), token.c_str());
}

static bool decodeChunkedBody(const std::string& encoded, std::string& decoded, std::string& errorMessage)
{
	decoded.assign(kHttpMaxBodyBytes, '\0');
	int decodedLen = 0;
	char error[160];
	const bool ok = httpSharedDecodeChunkedBody(encoded.data(), static_cast<int>(encoded.size()),
		&decoded[0], static_cast<int>(decoded.size()), &decodedLen, error, sizeof(error));
	if (!ok) {
		errorMessage = error;
		decoded.clear();
		return false;
	}
	decoded.resize(static_cast<size_t>(decodedLen));
	return true;
}

static bool isRedirectStatus(int statusCode)
{
	return httpSharedIsRedirectStatus(statusCode);
}

static std::string resolveHttpReference(const std::string& baseUrl, const std::string& ref)
{
	if (ref.empty()) return baseUrl;
	size_t colon = ref.find(':');
	if (colon != std::string::npos) {
		bool scheme = colon > 0;
		for (size_t i = 0; i < colon && scheme; ++i) {
			unsigned char ch = static_cast<unsigned char>(ref[i]);
			scheme = std::isalnum(ch) || ref[i] == '+' || ref[i] == '-' || ref[i] == '.';
		}
		if (scheme) return ref;
	}

	ParsedHttpUrl base = parseHttpUrl(baseUrl);
	if (!base.valid) return ref;
	if (ref.rfind("//", 0) == 0) return base.scheme + ":" + ref;
	if (ref[0] == '/') return base.origin() + ref;
	if (ref[0] == '#') return baseUrl;

	std::string path = base.path.empty() ? "/" : base.path;
	size_t slash = path.rfind('/');
	std::string dir = slash == std::string::npos ? "/" : path.substr(0, slash + 1);
	return base.origin() + dir + ref;
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
	response.transferEncoding = toLowerAscii(trimAscii(response.headerValue("Transfer-Encoding")));
	response.contentEncoding = toLowerAscii(trimAscii(response.headerValue("Content-Encoding")));

	if (isRedirectStatus(response.statusCode)) {
		return true;
	}

	if (!response.transferEncoding.empty() && !hasHeaderToken(response.transferEncoding, "identity")) {
		if (hasHeaderToken(response.transferEncoding, "chunked")) {
			std::string decoded;
			std::string chunkError;
			if (!decodeChunkedBody(response.body, decoded, chunkError)) {
				setError(response, chunkError.find("safety limit") != std::string::npos
					? HttpError::BodyTooLarge
					: HttpError::MalformedChunkedEncoding,
					chunkError);
				return false;
			}
			response.body = std::move(decoded);
		} else {
			setError(response, HttpError::UnsupportedTransferEncoding,
				"Unsupported Transfer-Encoding: " + response.transferEncoding);
			return false;
		}
	} else if (response.body.size() > kHttpMaxBodyBytes) {
		setError(response, HttpError::BodyTooLarge, "HTTP response body exceeded the safety limit.");
		return false;
	}

	if (!response.contentEncoding.empty() && !hasHeaderToken(response.contentEncoding, "identity")) {
		setError(response, HttpError::UnsupportedContentEncoding,
			"Unsupported Content-Encoding: " + response.contentEncoding);
		return false;
	}

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

struct WinsockHttpByteStreamContext {
	SOCKET socket = INVALID_SOCKET;
};

static std::size_t s_plainTcpByteStreamOpenCount = 0;
static std::size_t s_tlsByteStreamOpenCount = 0;

static int winsockHttpByteStreamRead(void* context, uint8_t* buffer, int length)
{
	WinsockHttpByteStreamContext* tcp = static_cast<WinsockHttpByteStreamContext*>(context);
	if (!tcp || tcp->socket == INVALID_SOCKET || !buffer || length <= 0) return -1;
	return recv(tcp->socket, reinterpret_cast<char*>(buffer), length, 0);
}

static int winsockHttpByteStreamWrite(void* context, const uint8_t* buffer, int length)
{
	WinsockHttpByteStreamContext* tcp = static_cast<WinsockHttpByteStreamContext*>(context);
	if (!tcp || tcp->socket == INVALID_SOCKET || !buffer || length <= 0) return -1;
	return send(tcp->socket, reinterpret_cast<const char*>(buffer), length, 0);
}

static void winsockHttpByteStreamClose(void* context)
{
	WinsockHttpByteStreamContext* tcp = static_cast<WinsockHttpByteStreamContext*>(context);
	if (!tcp || tcp->socket == INVALID_SOCKET) return;
	closesocket(tcp->socket);
	tcp->socket = INVALID_SOCKET;
}

static HttpByteStream makeWinsockHttpByteStream(WinsockHttpByteStreamContext& context, SOCKET socket)
{
	context.socket = socket;
	++s_plainTcpByteStreamOpenCount;
	return HttpByteStream{
		&context,
		winsockHttpByteStreamRead,
		winsockHttpByteStreamWrite,
		winsockHttpByteStreamClose,
	};
}

struct HostedTlsByteStreamContext {
	HttpByteStream tcp = {};
	CredHandle credentials = {};
	CtxtHandle securityContext = {};
	SecPkgContext_StreamSizes streamSizes = {};
	bool credentialsReady = false;
	bool contextReady = false;
	bool closed = false;
	bool smokeSelfSignedBypass = false;
	HttpError lastError = HttpError::None;
	std::string lastErrorMessage;
	std::string certificateValidation = "enabled";
	std::string status = "not started";
	std::vector<uint8_t> encrypted;
	std::vector<uint8_t> plaintext;
};

static std::string securityStatusHex(SECURITY_STATUS status)
{
	std::ostringstream out;
	out << "0x" << std::hex << std::uppercase
		<< static_cast<unsigned long>(status);
	return out.str();
}

static void setTlsError(HostedTlsByteStreamContext& tls, HttpError error,
	const std::string& message, SECURITY_STATUS status = SEC_E_OK)
{
	tls.lastError = error;
	tls.lastErrorMessage = message;
	if (status != SEC_E_OK) {
		tls.lastErrorMessage += " (Schannel " + securityStatusHex(status) + ")";
	}
	tls.status = "error";
}

static bool tcpWriteAll(HttpByteStream& stream, const uint8_t* bytes, size_t byteCount)
{
	size_t sent = 0;
	while (sent < byteCount) {
		const int chunk = static_cast<int>(std::min<size_t>(byteCount - sent, 0x7fffffff));
		int n = stream.write(stream.context, bytes + sent, chunk);
		if (n <= 0) return false;
		sent += static_cast<size_t>(n);
	}
	return true;
}

static bool allowSmokeSelfSignedLocalhost(const std::string& hostname)
{
	const char* flag = std::getenv("GXOS_NAVIGATOR_SMOKE_ALLOW_SELF_SIGNED_LOCALHOST");
	return flag && std::string(flag) == "1" && toLowerAscii(hostname) == "localhost";
}

static bool verifyHostedTlsCertificate(HostedTlsByteStreamContext& tls, const std::string& hostname)
{
	PCCERT_CONTEXT certificate = nullptr;
	SECURITY_STATUS query = QueryContextAttributes(&tls.securityContext,
		SECPKG_ATTR_REMOTE_CERT_CONTEXT, &certificate);
	if (query != SEC_E_OK || !certificate) {
		setTlsError(tls, HttpError::TlsCertificateValidationFailed,
			"TLS certificate validation failed because the server certificate was unavailable.", query);
		return false;
	}

	CERT_CHAIN_PARA chainParameters = {};
	chainParameters.cbSize = sizeof(chainParameters);
	PCCERT_CHAIN_CONTEXT chain = nullptr;
	BOOL chainOk = CertGetCertificateChain(nullptr, certificate, nullptr,
		certificate->hCertStore, &chainParameters, 0, nullptr, &chain);
	if (!chainOk || !chain) {
		CertFreeCertificateContext(certificate);
		setTlsError(tls, HttpError::TlsCertificateValidationFailed,
			"TLS certificate validation failed while building the Windows certificate chain.");
		return false;
	}

	std::wstring wideHostname(hostname.begin(), hostname.end());
	SSL_EXTRA_CERT_CHAIN_POLICY_PARA sslPolicy = {};
	sslPolicy.cbSize = sizeof(sslPolicy);
	sslPolicy.dwAuthType = AUTHTYPE_SERVER;
	sslPolicy.pwszServerName = wideHostname.empty() ? nullptr : &wideHostname[0];

	CERT_CHAIN_POLICY_PARA policyParameters = {};
	policyParameters.cbSize = sizeof(policyParameters);
	policyParameters.pvExtraPolicyPara = &sslPolicy;
	CERT_CHAIN_POLICY_STATUS policyStatus = {};
	policyStatus.cbSize = sizeof(policyStatus);
	BOOL policyOk = CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL,
		chain, &policyParameters, &policyStatus);

	bool accepted = policyOk && policyStatus.dwError == 0;
	if (!accepted && allowSmokeSelfSignedLocalhost(hostname) &&
		policyStatus.dwError == static_cast<DWORD>(CERT_E_UNTRUSTEDROOT)) {
		accepted = true;
		tls.smokeSelfSignedBypass = true;
		tls.certificateValidation = "enabled; smoke-only localhost self-signed bypass active";
	}

	if (!accepted) {
		const DWORD error = policyStatus.dwError;
		std::ostringstream detail;
		detail << "TLS certificate validation failed for " << hostname
			<< " (Windows trust error 0x" << std::hex << std::uppercase << error << ").";
		if (error == static_cast<DWORD>(CERT_E_CN_NO_MATCH)) {
			setTlsError(tls, HttpError::TlsCertificateHostnameMismatch,
				"TLS certificate hostname mismatch for " + hostname + ".");
		} else if (error == static_cast<DWORD>(CERT_E_EXPIRED) ||
			error == static_cast<DWORD>(CERT_E_VALIDITYPERIODNESTING)) {
			setTlsError(tls, HttpError::TlsCertificateExpired,
				"TLS certificate is expired or not yet valid for " + hostname + ".");
		} else {
			setTlsError(tls, HttpError::TlsCertificateValidationFailed, detail.str());
		}
	}

	CertFreeCertificateChain(chain);
	CertFreeCertificateContext(certificate);
	return accepted;
}

static void hostedTlsClose(void* context)
{
	HostedTlsByteStreamContext* tls = static_cast<HostedTlsByteStreamContext*>(context);
	if (!tls || tls->closed) return;
	tls->closed = true;
	if (tls->contextReady) {
		DeleteSecurityContext(&tls->securityContext);
		tls->contextReady = false;
	}
	if (tls->credentialsReady) {
		FreeCredentialsHandle(&tls->credentials);
		tls->credentialsReady = false;
	}
	if (tls->tcp.close) tls->tcp.close(tls->tcp.context);
}

static bool hostedTlsHandshake(HostedTlsByteStreamContext& tls, const std::string& hostname)
{
	SCHANNEL_CRED credentialData = {};
	credentialData.dwVersion = SCHANNEL_CRED_VERSION;
	credentialData.grbitEnabledProtocols = SP_PROT_TLS1_2_CLIENT | SP_PROT_TLS1_3_CLIENT;
	credentialData.dwFlags = SCH_CRED_MANUAL_CRED_VALIDATION |
		SCH_CRED_NO_DEFAULT_CREDS | SCH_USE_STRONG_CRYPTO;
	TimeStamp expiry = {};
	SECURITY_STATUS status = AcquireCredentialsHandleA(nullptr,
		const_cast<char*>(UNISP_NAME_A), SECPKG_CRED_OUTBOUND, nullptr,
		&credentialData, nullptr, nullptr, &tls.credentials, &expiry);
	if (status != SEC_E_OK) {
		setTlsError(tls, HttpError::TlsHandshakeFailed,
			"TLS handshake failed while acquiring Schannel credentials.", status);
		return false;
	}
	tls.credentialsReady = true;

	const ULONG requestFlags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT |
		ISC_REQ_CONFIDENTIALITY | ISC_REQ_EXTENDED_ERROR |
		ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM;
	ULONG contextFlags = 0;
	bool firstCall = true;
	for (;;) {
		SecBuffer inBuffers[2] = {};
		SecBufferDesc input = {};
		if (!firstCall) {
			if (tls.encrypted.empty()) {
				uint8_t buffer[4096];
				int received = tls.tcp.read(tls.tcp.context, buffer, sizeof(buffer));
				if (received <= 0) {
					setTlsError(tls, HttpError::TlsHandshakeFailed,
						"TLS handshake failed while reading the server response.");
					return false;
				}
				tls.encrypted.insert(tls.encrypted.end(), buffer, buffer + received);
			}
			inBuffers[0].BufferType = SECBUFFER_TOKEN;
			inBuffers[0].pvBuffer = tls.encrypted.data();
			inBuffers[0].cbBuffer = static_cast<unsigned long>(tls.encrypted.size());
			inBuffers[1].BufferType = SECBUFFER_EMPTY;
			input.ulVersion = SECBUFFER_VERSION;
			input.cBuffers = 2;
			input.pBuffers = inBuffers;
		}

		SecBuffer outBuffer = {};
		outBuffer.BufferType = SECBUFFER_TOKEN;
		SecBufferDesc output = {};
		output.ulVersion = SECBUFFER_VERSION;
		output.cBuffers = 1;
		output.pBuffers = &outBuffer;
		status = InitializeSecurityContextA(&tls.credentials,
			firstCall ? nullptr : &tls.securityContext,
			const_cast<char*>(hostname.c_str()), requestFlags, 0,
			SECURITY_NATIVE_DREP, firstCall ? nullptr : &input, 0,
			&tls.securityContext, &output, &contextFlags, &expiry);
		tls.contextReady = true;
		firstCall = false;

		if (outBuffer.pvBuffer && outBuffer.cbBuffer > 0) {
			const bool sent = tcpWriteAll(tls.tcp,
				static_cast<const uint8_t*>(outBuffer.pvBuffer), outBuffer.cbBuffer);
			FreeContextBuffer(outBuffer.pvBuffer);
			if (!sent) {
				setTlsError(tls, HttpError::TlsHandshakeFailed,
					"TLS handshake failed while sending a Schannel token.");
				return false;
			}
		}

		if (status == SEC_E_INCOMPLETE_MESSAGE) {
			uint8_t buffer[4096];
			int received = tls.tcp.read(tls.tcp.context, buffer, sizeof(buffer));
			if (received <= 0) {
				setTlsError(tls, HttpError::TlsHandshakeFailed,
					"TLS handshake ended before the server response completed.");
				return false;
			}
			tls.encrypted.insert(tls.encrypted.end(), buffer, buffer + received);
			continue;
		}

		std::vector<uint8_t> extra;
		if (!tls.encrypted.empty() && inBuffers[1].BufferType == SECBUFFER_EXTRA) {
			const size_t count = inBuffers[1].cbBuffer;
			extra.assign(tls.encrypted.end() - count, tls.encrypted.end());
		}
		tls.encrypted.swap(extra);

		if (status == SEC_E_OK) break;
		if (status != SEC_I_CONTINUE_NEEDED) {
			setTlsError(tls,
				status == SEC_E_ALGORITHM_MISMATCH ? HttpError::TlsProtocolUnsupported : HttpError::TlsHandshakeFailed,
				status == SEC_E_ALGORITHM_MISMATCH
					? "TLS protocol negotiation failed because no supported protocol or cipher matched."
					: "TLS handshake failed during Schannel negotiation.",
				status);
			return false;
		}
	}

	status = QueryContextAttributes(&tls.securityContext,
		SECPKG_ATTR_STREAM_SIZES, &tls.streamSizes);
	if (status != SEC_E_OK) {
		setTlsError(tls, HttpError::TlsHandshakeFailed,
			"TLS handshake completed but Schannel stream sizes were unavailable.", status);
		return false;
	}
	if (!verifyHostedTlsCertificate(tls, hostname)) return false;
	tls.status = "connected";
	return true;
}

static int hostedTlsRead(void* context, uint8_t* buffer, int length)
{
	HostedTlsByteStreamContext* tls = static_cast<HostedTlsByteStreamContext*>(context);
	if (!tls || tls->closed || !buffer || length <= 0) return -1;
	for (;;) {
		if (!tls->plaintext.empty()) {
			const size_t count = std::min<size_t>(tls->plaintext.size(), static_cast<size_t>(length));
			std::copy(tls->plaintext.begin(), tls->plaintext.begin() + count, buffer);
			tls->plaintext.erase(tls->plaintext.begin(), tls->plaintext.begin() + count);
			return static_cast<int>(count);
		}
		if (tls->encrypted.empty()) {
			uint8_t encrypted[4096];
			int received = tls->tcp.read(tls->tcp.context, encrypted, sizeof(encrypted));
			if (received <= 0) return received;
			tls->encrypted.insert(tls->encrypted.end(), encrypted, encrypted + received);
		}

		SecBuffer buffers[4] = {};
		buffers[0].BufferType = SECBUFFER_DATA;
		buffers[0].pvBuffer = tls->encrypted.data();
		buffers[0].cbBuffer = static_cast<unsigned long>(tls->encrypted.size());
		for (int i = 1; i < 4; ++i) buffers[i].BufferType = SECBUFFER_EMPTY;
		SecBufferDesc message = {};
		message.ulVersion = SECBUFFER_VERSION;
		message.cBuffers = 4;
		message.pBuffers = buffers;
		SECURITY_STATUS status = DecryptMessage(&tls->securityContext, &message, 0, nullptr);
		if (status == SEC_E_INCOMPLETE_MESSAGE) {
			uint8_t encrypted[4096];
			int received = tls->tcp.read(tls->tcp.context, encrypted, sizeof(encrypted));
			if (received <= 0) {
				setTlsError(*tls, HttpError::TlsReadFailed,
					"TLS read failed because the encrypted response ended mid-record.");
				return -1;
			}
			tls->encrypted.insert(tls->encrypted.end(), encrypted, encrypted + received);
			continue;
		}
		if (status == SEC_I_CONTEXT_EXPIRED) return 0;
		if (status != SEC_E_OK) {
			setTlsError(*tls, HttpError::TlsReadFailed,
				status == SEC_I_RENEGOTIATE
					? "TLS read failed because renegotiation is not supported by this prototype."
					: "TLS read failed while Schannel decrypted the server response.",
				status);
			return -1;
		}

		std::vector<uint8_t> extra;
		for (const SecBuffer& item : buffers) {
			if (item.BufferType == SECBUFFER_DATA && item.pvBuffer && item.cbBuffer) {
				const uint8_t* data = static_cast<const uint8_t*>(item.pvBuffer);
				tls->plaintext.insert(tls->plaintext.end(), data, data + item.cbBuffer);
			} else if (item.BufferType == SECBUFFER_EXTRA && item.cbBuffer) {
				extra.assign(tls->encrypted.end() - item.cbBuffer, tls->encrypted.end());
			}
		}
		tls->encrypted.swap(extra);
	}
}

static int hostedTlsWrite(void* context, const uint8_t* buffer, int length)
{
	HostedTlsByteStreamContext* tls = static_cast<HostedTlsByteStreamContext*>(context);
	if (!tls || tls->closed || !buffer || length <= 0) return -1;
	int offset = 0;
	while (offset < length) {
		const int count = std::min<int>(length - offset, tls->streamSizes.cbMaximumMessage);
		std::vector<uint8_t> encrypted(tls->streamSizes.cbHeader + count + tls->streamSizes.cbTrailer);
		std::copy(buffer + offset, buffer + offset + count,
			encrypted.begin() + tls->streamSizes.cbHeader);
		SecBuffer buffers[4] = {};
		buffers[0].BufferType = SECBUFFER_STREAM_HEADER;
		buffers[0].pvBuffer = encrypted.data();
		buffers[0].cbBuffer = tls->streamSizes.cbHeader;
		buffers[1].BufferType = SECBUFFER_DATA;
		buffers[1].pvBuffer = encrypted.data() + tls->streamSizes.cbHeader;
		buffers[1].cbBuffer = count;
		buffers[2].BufferType = SECBUFFER_STREAM_TRAILER;
		buffers[2].pvBuffer = encrypted.data() + tls->streamSizes.cbHeader + count;
		buffers[2].cbBuffer = tls->streamSizes.cbTrailer;
		buffers[3].BufferType = SECBUFFER_EMPTY;
		SecBufferDesc message = {};
		message.ulVersion = SECBUFFER_VERSION;
		message.cBuffers = 4;
		message.pBuffers = buffers;
		SECURITY_STATUS status = EncryptMessage(&tls->securityContext, 0, &message, 0);
		if (status != SEC_E_OK) {
			setTlsError(*tls, HttpError::TlsWriteFailed,
				"TLS write failed while Schannel encrypted the request.", status);
			return -1;
		}
		const size_t encryptedBytes = buffers[0].cbBuffer + buffers[1].cbBuffer + buffers[2].cbBuffer;
		if (!tcpWriteAll(tls->tcp, encrypted.data(), encryptedBytes)) {
			setTlsError(*tls, HttpError::TlsWriteFailed,
				"TLS write failed while sending the encrypted request.");
			return -1;
		}
		offset += count;
	}
	return length;
}

static bool createHostedTlsStream(HostedTlsByteStreamContext& context,
	const std::string& hostname, HttpByteStream tcp, HttpByteStream& out)
{
	context.tcp = tcp;
	++s_tlsByteStreamOpenCount;
	if (!hostedTlsHandshake(context, hostname)) return false;
	out = HttpByteStream{
		&context,
		hostedTlsRead,
		hostedTlsWrite,
		hostedTlsClose,
	};
	return true;
}

static void copyTlsDiagnostics(HttpResponse& response, const HostedTlsByteStreamContext& tls)
{
	response.tlsBackend = "Schannel hosted";
	response.tlsCertificateValidation = tls.certificateValidation;
	response.tlsStatus = tls.status;
	response.tlsError = tls.lastErrorMessage;
	response.tlsSmokeSelfSignedBypass = tls.smokeSelfSignedBypass;
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
	if (!((scheme == "http" && port == 80) || (scheme == "https" && port == 443))) {
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
	if (!isSupportedHttpScheme(parsed.scheme)) {
		parsed.error = "Only http:// and https:// URLs are supported by this HTTP client.";
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
		parsed.port = parsed.scheme == "https" ? 443 : 80;
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
	case HttpError::RedirectLimitExceeded: return "RedirectLimitExceeded";
	case HttpError::UnsupportedTransferEncoding: return "UnsupportedTransferEncoding";
	case HttpError::UnsupportedContentEncoding: return "UnsupportedContentEncoding";
	case HttpError::MalformedChunkedEncoding: return "MalformedChunkedEncoding";
	case HttpError::TlsHandshakeFailed: return "TlsHandshakeFailed";
	case HttpError::TlsCertificateValidationFailed: return "TlsCertificateValidationFailed";
	case HttpError::TlsCertificateHostnameMismatch: return "TlsCertificateHostnameMismatch";
	case HttpError::TlsCertificateExpired: return "TlsCertificateExpired";
	case HttpError::TlsProtocolUnsupported: return "TlsProtocolUnsupported";
	case HttpError::TlsReadFailed: return "TlsReadFailed";
	case HttpError::TlsWriteFailed: return "TlsWriteFailed";
	}
	return "Unknown";
}

std::size_t httpPlainTcpByteStreamOpenCount()
{
#if defined(_WIN32)
	return s_plainTcpByteStreamOpenCount;
#else
	return 0;
#endif
}

std::string HttpResponse::headerValue(const std::string& name) const
{
	const std::string needle = toLowerAscii(name);
	for (const HttpHeader& header : headers) {
		if (toLowerAscii(header.name) == needle) return header.value;
	}
	return "";
}

static HttpResponse sendSingleHttpRequest(const std::string& url,
	const std::string& method,
	const std::string& body,
	const std::string& contentType)
{
	HttpResponse response;
	response.requestedUrl = url;
	response.finalUrl = url;
	ParsedHttpUrl parsed = parseHttpUrl(url);
	if (!parsed.valid) {
		setError(response,
			parsed.scheme.empty() || isSupportedHttpScheme(parsed.scheme) ? HttpError::InvalidUrl : HttpError::UnsupportedScheme,
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
	WinsockHttpByteStreamContext tcpContext;
	HttpByteStream tcpStream = makeWinsockHttpByteStream(tcpContext, sock);
	HttpByteStream stream = tcpStream;
	HostedTlsByteStreamContext tlsContext;
	HostedTlsByteStreamContext* activeTls = nullptr;
	if (parsed.scheme == "https") {
		response.tlsBackend = "Schannel hosted";
		response.tlsCertificateValidation = "enabled";
		if (!createHostedTlsStream(tlsContext, parsed.host, tcpStream, stream)) {
			copyTlsDiagnostics(response, tlsContext);
			hostedTlsClose(&tlsContext);
			setError(response,
				tlsContext.lastError == HttpError::None ? HttpError::TlsHandshakeFailed : tlsContext.lastError,
				tlsContext.lastErrorMessage.empty() ? "TLS handshake failed." : tlsContext.lastErrorMessage);
			return response;
		}
		activeTls = &tlsContext;
		copyTlsDiagnostics(response, tlsContext);
	}

	const bool isPost = toLowerAscii(method) == "post";
	std::ostringstream request;
	request << (isPost ? "POST" : "GET") << " " << parsed.requestTarget() << " HTTP/1.0\r\n"
		<< "Host: " << parsed.host;
	if (!((parsed.scheme == "http" && parsed.port == 80) ||
		(parsed.scheme == "https" && parsed.port == 443))) request << ":" << parsed.port;
	request << "\r\n"
		<< "User-Agent: guideXOS-Navigator/0.1\r\n"
		<< "Accept-Encoding: identity\r\n"
		<< "Connection: close\r\n";
	if (isPost) {
		request << "Content-Type: " << (contentType.empty() ? "application/x-www-form-urlencoded" : contentType) << "\r\n"
			<< "Content-Length: " << body.size() << "\r\n";
	}
	request << "\r\n";
	if (isPost) request << body;
	const std::string requestText = request.str();
	size_t sentTotal = 0;
	while (sentTotal < requestText.size()) {
		int sent = stream.write(stream.context,
			reinterpret_cast<const uint8_t*>(requestText.data() + sentTotal),
			static_cast<int>(requestText.size() - sentTotal));
		if (sent <= 0) {
			if (activeTls) copyTlsDiagnostics(response, *activeTls);
			stream.close(stream.context);
			setError(response,
				activeTls && activeTls->lastError != HttpError::None ? activeTls->lastError : HttpError::SendFailed,
				activeTls && !activeTls->lastErrorMessage.empty()
					? activeTls->lastErrorMessage : "Failed while sending HTTP request.");
			return response;
		}
		sentTotal += static_cast<size_t>(sent);
	}

	std::string raw;
	raw.reserve(16u * 1024u);
	bool sawHeaderEnd = false;
	bool rawChunked = false;
	size_t headerBytes = 0;
	char buffer[4096];
	for (;;) {
		int received = stream.read(stream.context,
			reinterpret_cast<uint8_t*>(buffer), static_cast<int>(sizeof(buffer)));
		if (received == 0) break;
		if (received < 0) {
			int error = WSAGetLastError();
			if (activeTls) copyTlsDiagnostics(response, *activeTls);
			stream.close(stream.context);
			setError(response,
				activeTls && activeTls->lastError != HttpError::None
					? activeTls->lastError
					: (error == WSAETIMEDOUT ? HttpError::Timeout : HttpError::ReceiveFailed),
				activeTls && !activeTls->lastErrorMessage.empty()
					? activeTls->lastErrorMessage
					: (error == WSAETIMEDOUT ? "Timed out while reading HTTP response." : "Failed while reading HTTP response."));
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
				rawChunked = toLowerAscii(raw.substr(0, end)).find("transfer-encoding:") != std::string::npos &&
					toLowerAscii(raw.substr(0, end)).find("chunked") != std::string::npos;
			} else if (raw.size() > kHttpMaxHeaderBytes) {
				stream.close(stream.context);
				setError(response, HttpError::HeaderTooLarge, "HTTP response headers exceeded the safety limit.");
				return response;
			}
		}
		const size_t rawBodyLimit = kHttpMaxBodyBytes + (rawChunked ? kHttpMaxHeaderBytes : 0u);
		if (sawHeaderEnd && raw.size() > headerBytes + rawBodyLimit) {
			stream.close(stream.context);
			setError(response, HttpError::BodyTooLarge, "HTTP response body exceeded the safety limit.");
			return response;
		}
	}
	if (activeTls) copyTlsDiagnostics(response, *activeTls);
	stream.close(stream.context);

	parseHttpResponse(raw, response);
	return response;
#else
	setError(response, HttpError::NetworkUnavailable, "HTTP networking is unavailable in this runtime.");
	return response;
#endif
}

static HttpResponse sendHttpRequestWithRedirects(const std::string& url,
	const std::string& method,
	const std::string& body,
	const std::string& contentType)
{
	std::string currentUrl = url;
	std::vector<std::string> chain;
	std::string currentMethod = toLowerAscii(method) == "post" ? "post" : "get";
	std::string currentBody = body;

	for (int redirectCount = 0; redirectCount <= kHttpMaxRedirects; ++redirectCount) {
		HttpResponse response = sendSingleHttpRequest(currentUrl, currentMethod, currentBody, contentType);
		response.requestedUrl = url;
		response.finalUrl = currentUrl;
		response.redirectCount = redirectCount;
		response.redirectChain = chain;
		if (!response.ok()) return response;

		if (!isRedirectStatus(response.statusCode)) {
			return response;
		}

		const std::string location = response.headerValue("Location");
		if (location.empty()) {
			return response;
		}
		if (redirectCount == kHttpMaxRedirects) {
			setError(response, HttpError::RedirectLimitExceeded,
				"HTTP redirect limit exceeded while loading: " + url);
			return response;
		}

		std::string nextUrl = resolveHttpReference(currentUrl, location);
		ParsedHttpUrl parsedNext = parseHttpUrl(nextUrl);
		if (!parsedNext.valid) {
			chain.push_back(nextUrl);
			response.finalUrl = nextUrl;
			response.redirectCount = redirectCount + 1;
			response.redirectChain = chain;
			setError(response,
				parsedNext.scheme.empty() || isSupportedHttpScheme(parsedNext.scheme) ? HttpError::InvalidUrl : HttpError::UnsupportedScheme,
				"Redirect Location is unsupported or invalid: " + location);
			return response;
		}
		chain.push_back(nextUrl);
		currentUrl = nextUrl;
		if (response.statusCode == 303) {
			currentMethod = "get";
			currentBody.clear();
		}
	}

	HttpResponse response;
	response.requestedUrl = url;
	response.finalUrl = currentUrl;
	response.redirectChain = chain;
	response.redirectCount = static_cast<int>(chain.size());
	setError(response, HttpError::RedirectLimitExceeded,
		"HTTP redirect limit exceeded while loading: " + url);
	return response;
}

HttpResponse fetchHttpUrl(const std::string& url)
{
	return sendHttpRequestWithRedirects(url, "get", "", "");
}

HttpResponse postHttpUrl(const std::string& url, const std::string& body, const std::string& contentType)
{
	return sendHttpRequestWithRedirects(url, "post", body, contentType);
}

} // namespace web
} // namespace gxos
