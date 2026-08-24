#include "navigator_resource_diagnostics.h"

#include <iostream>
#include <string>

using namespace gxos::apps;

namespace {

bool expect(bool condition, const char* label)
{
    if (!condition) std::cerr << "FAIL: " << label << "\n";
    return condition;
}

bool expectName(const NavigatorResourceClassificationInput& input,
                NavigatorResourceClassification expected, const char* label)
{
    const NavigatorResourceClassification actual = classifyNavigatorResource(input);
    return expect(actual == expected, label) &&
        expect(std::string(navigatorResourceClassificationName(actual)) ==
            navigatorResourceClassificationName(expected), "stable category name");
}

} // namespace

int main()
{
    bool ok = true;

    NavigatorResourceClassificationInput loadedPng;
    loadedPng.loaded = true;
    loadedPng.imageFormat = NavigatorResourceImageFormat::Png;
    ok &= expectName(loadedPng, NavigatorResourceClassification::LoadedPng, "valid PNG");

    NavigatorResourceClassificationInput loadedJpeg;
    loadedJpeg.loaded = true;
    loadedJpeg.imageFormat = NavigatorResourceImageFormat::Jpeg;
    ok &= expectName(loadedJpeg, NavigatorResourceClassification::LoadedJpeg, "valid JPEG");

    NavigatorResourceClassificationInput svg;
    svg.mimeFailure = NavigatorResourceMimeFailure::UnsupportedSvg;
    ok &= expectName(svg, NavigatorResourceClassification::UnsupportedSvg, "SVG MIME unsupported");
    NavigatorResourceClassificationInput webp;
    webp.mimeFailure = NavigatorResourceMimeFailure::UnsupportedWebp;
    ok &= expectName(webp, NavigatorResourceClassification::UnsupportedWebp, "WebP MIME unsupported");
    NavigatorResourceClassificationInput gif;
    gif.mimeFailure = NavigatorResourceMimeFailure::UnsupportedGif;
    ok &= expectName(gif, NavigatorResourceClassification::UnsupportedGif, "GIF MIME unsupported");
    NavigatorResourceClassificationInput avif;
    avif.mimeFailure = NavigatorResourceMimeFailure::UnsupportedAvif;
    ok &= expectName(avif, NavigatorResourceClassification::UnsupportedAvif, "AVIF MIME unsupported");

    NavigatorResourceClassificationInput missingMime;
    missingMime.mimeFailure = NavigatorResourceMimeFailure::ContentTypeMissing;
    ok &= expectName(missingMime, NavigatorResourceClassification::ContentTypeMissing, "missing Content-Type");
    NavigatorResourceClassificationInput wrongMime;
    wrongMime.mimeFailure = NavigatorResourceMimeFailure::ContentTypeMismatch;
    ok &= expectName(wrongMime, NavigatorResourceClassification::ContentTypeMismatch, "wrong Content-Type");
    NavigatorResourceClassificationInput extensionMismatch;
    extensionMismatch.mimeFailure = NavigatorResourceMimeFailure::MimeExtensionDisagreement;
    ok &= expectName(extensionMismatch, NavigatorResourceClassification::MimeExtensionDisagreement, "extension/MIME mismatch");

    NavigatorResourceClassificationInput notFound;
    notFound.httpStatusCode = 404;
    ok &= expectName(notFound, NavigatorResourceClassification::HttpStatus4xx, "HTTP 404");
    NavigatorResourceClassificationInput serverError;
    serverError.httpStatusCode = 500;
    ok &= expectName(serverError, NavigatorResourceClassification::HttpStatus5xx, "HTTP 500");

    // A successful redirect is still a successful resource outcome; redirect
    // count is retained as bounded telemetry by the Navigator integration.
    NavigatorResourceClassificationInput redirected;
    redirected.loaded = true;
    redirected.imageFormat = NavigatorResourceImageFormat::Jpeg;
    ok &= expectName(redirected, NavigatorResourceClassification::LoadedJpeg, "redirect to valid resource");
    NavigatorResourceClassificationInput redirectLoop;
    redirectLoop.httpFailure = NavigatorResourceHttpFailure::RedirectLimit;
    ok &= expectName(redirectLoop, NavigatorResourceClassification::HttpRedirectLimit, "redirect loop");

    NavigatorResourceClassificationInput relativeFailure;
    relativeFailure.policyFailure = NavigatorResourcePolicyFailure::RelativeUrlResolutionFailed;
    ok &= expectName(relativeFailure, NavigatorResourceClassification::RelativeUrlResolutionFailed, "relative resource URL");
    // Cross-origin fetches are in scope and therefore remain a normal success;
    // same-origin/cross-origin is recorded in telemetry rather than treated as
    // a failure without a policy rejection.
    NavigatorResourceClassificationInput crossOrigin;
    crossOrigin.loaded = true;
    crossOrigin.imageFormat = NavigatorResourceImageFormat::Png;
    ok &= expectName(crossOrigin, NavigatorResourceClassification::LoadedPng, "cross-origin resource");

    NavigatorResourceClassificationInput encodedLimit;
    encodedLimit.httpFailure = NavigatorResourceHttpFailure::BodyTooLargeEncoded;
    ok &= expectName(encodedLimit, NavigatorResourceClassification::BodyTooLargeEncoded, "encoded body over limit");
    NavigatorResourceClassificationInput decodedLimit;
    decodedLimit.contentEncodingFailure = NavigatorResourceContentEncodingFailure::DecodedTooLarge;
    ok &= expectName(decodedLimit, NavigatorResourceClassification::DecodedResourceTooLarge, "decoded body over limit");
    NavigatorResourceClassificationInput malformedGzip;
    malformedGzip.contentEncodingFailure = NavigatorResourceContentEncodingFailure::MalformedGzip;
    ok &= expectName(malformedGzip, NavigatorResourceClassification::MalformedGzip, "malformed gzip");
    NavigatorResourceClassificationInput malformedImage;
    malformedImage.decodeFailure = NavigatorResourceDecodeFailure::CorruptImage;
    ok &= expectName(malformedImage, NavigatorResourceClassification::CorruptImage, "malformed image");
    NavigatorResourceClassificationInput dimensions;
    dimensions.decodeFailure = NavigatorResourceDecodeFailure::DimensionsTooLarge;
    ok &= expectName(dimensions, NavigatorResourceClassification::ImageDimensionsTooLarge, "image dimensions over cap");
    NavigatorResourceClassificationInput resourceCap;
    resourceCap.policyFailure = NavigatorResourcePolicyFailure::ResourceLimitReached;
    ok &= expectName(resourceCap, NavigatorResourceClassification::ResourceLimitReached, "resource-count cap");
    NavigatorResourceClassificationInput duplicate;
    duplicate.lifecycleFailure = NavigatorResourceLifecycleFailure::DuplicateResourceSkipped;
    ok &= expectName(duplicate, NavigatorResourceClassification::DuplicateResourceSkipped, "duplicate resource");
    NavigatorResourceClassificationInput timeout;
    timeout.transportFailure = NavigatorResourceTransportFailure::TimeoutHttp;
    ok &= expectName(timeout, NavigatorResourceClassification::TimeoutHttp, "timeout classification");

    std::cout << (ok ? "Navigator resource diagnostics tests PASS\n"
                     : "Navigator resource diagnostics tests FAIL\n");
    return ok ? 0 : 1;
}
