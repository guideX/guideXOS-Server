#pragma once

// Bounded Navigator public-resource outcome taxonomy.  The input is deliberately
// made from fixed enums and scalar flags so the classifier can be used by both
// the hosted STL path and the bare-metal Navigator without retaining bodies or
// allocating diagnostic strings.

#include <stdint.h>

namespace gxos {
namespace apps {

enum class NavigatorResourceTransportFailure : uint8_t {
    None = 0,
    DnsFailed,
    TcpFailed,
    TlsFailed,
    CertificateFailed,
    HostnameFailed,
    TimeoutConnect,
    TimeoutTls,
    TimeoutHttp,
    ConnectionClosedIncomplete,
};

enum class NavigatorResourceHttpFailure : uint8_t {
    None = 0,
    RedirectLimit,
    RedirectInvalid,
    Status3xxUnresolved,
    Status4xx,
    Status5xx,
    StatusOther,
    BodyTooLargeEncoded,
    MalformedHttp,
    UnsupportedTransferEncoding,
};

enum class NavigatorResourceContentEncodingFailure : uint8_t {
    None = 0,
    Unsupported,
    MalformedGzip,
    MalformedDeflate,
    DecodedTooLarge,
};

enum class NavigatorResourceMimeFailure : uint8_t {
    None = 0,
    UnsupportedMime,
    ContentTypeMissing,
    ContentTypeMismatch,
    MimeExtensionDisagreement,
    UnsupportedSvg,
    UnsupportedWebp,
    UnsupportedAvif,
    UnsupportedGif,
    UnsupportedOtherImage,
};

enum class NavigatorResourcePolicyFailure : uint8_t {
    None = 0,
    UnsupportedScheme,
    MalformedUrl,
    RelativeUrlResolutionFailed,
    CrossOriginPolicyRejected,
    ReviewedTargetPolicyRejected,
    DataUrlUnsupported,
    ResourceLimitReached,
    InsecureRedirectBlocked,
};

enum class NavigatorResourceDecodeFailure : uint8_t {
    None = 0,
    PngDecodeFailed,
    JpegDecodeFailed,
    DimensionsTooLarge,
    PixelBudgetExceeded,
    AllocationFailed,
    UnsupportedJpegVariant,
    CorruptImage,
};

enum class NavigatorResourceLifecycleFailure : uint8_t {
    None = 0,
    DuplicateResourceSkipped,
    StaleResourceState,
    AttachmentFailed,
    ResourceSlotUnavailable,
};

enum class NavigatorResourceImageFormat : uint8_t {
    None = 0,
    Png,
    Jpeg,
    Other,
};

enum class NavigatorResourceClassification : uint8_t {
    LoadedPng = 0,
    LoadedJpeg,
    LoadedOtherExistingSupportedResource,
    DnsFailed,
    TcpFailed,
    TlsFailed,
    CertificateFailed,
    HostnameFailed,
    TimeoutConnect,
    TimeoutTls,
    TimeoutHttp,
    ConnectionClosedIncomplete,
    HttpRedirectLimit,
    HttpRedirectInvalid,
    HttpStatus3xxUnresolved,
    HttpStatus4xx,
    HttpStatus5xx,
    HttpStatusOther,
    BodyTooLargeEncoded,
    MalformedHttp,
    UnsupportedTransferEncoding,
    UnsupportedContentEncoding,
    MalformedGzip,
    MalformedDeflate,
    DecodedResourceTooLarge,
    UnsupportedMime,
    ContentTypeMissing,
    ContentTypeMismatch,
    MimeExtensionDisagreement,
    UnsupportedSvg,
    UnsupportedWebp,
    UnsupportedAvif,
    UnsupportedGif,
    UnsupportedOtherImage,
    UnsupportedScheme,
    MalformedUrl,
    RelativeUrlResolutionFailed,
    CrossOriginPolicyRejected,
    ReviewedTargetPolicyRejected,
    DataUrlUnsupported,
    ResourceLimitReached,
    InsecureRedirectBlocked,
    PngDecodeFailed,
    JpegDecodeFailed,
    ImageDimensionsTooLarge,
    ImagePixelBudgetExceeded,
    ImageAllocationFailed,
    UnsupportedJpegVariant,
    CorruptImage,
    DuplicateResourceSkipped,
    StaleResourceState,
    AttachmentFailed,
    ResourceSlotUnavailable,
    OtherFailure,
};

struct NavigatorResourceClassificationInput {
    NavigatorResourceTransportFailure transportFailure = NavigatorResourceTransportFailure::None;
    NavigatorResourceHttpFailure httpFailure = NavigatorResourceHttpFailure::None;
    NavigatorResourceContentEncodingFailure contentEncodingFailure = NavigatorResourceContentEncodingFailure::None;
    NavigatorResourceMimeFailure mimeFailure = NavigatorResourceMimeFailure::None;
    NavigatorResourcePolicyFailure policyFailure = NavigatorResourcePolicyFailure::None;
    NavigatorResourceDecodeFailure decodeFailure = NavigatorResourceDecodeFailure::None;
    NavigatorResourceLifecycleFailure lifecycleFailure = NavigatorResourceLifecycleFailure::None;
    NavigatorResourceImageFormat imageFormat = NavigatorResourceImageFormat::None;
    bool loaded = false;
    int httpStatusCode = 0;
};

inline NavigatorResourceClassification classifyNavigatorResource(
    const NavigatorResourceClassificationInput& input)
{
    // Lifecycle/policy decisions happen before any fetch.  Transport, HTTP,
    // decoding, and success are then evaluated in order.  This precedence is
    // intentional: one resource always receives one primary outcome.
    switch (input.lifecycleFailure) {
    case NavigatorResourceLifecycleFailure::DuplicateResourceSkipped: return NavigatorResourceClassification::DuplicateResourceSkipped;
    case NavigatorResourceLifecycleFailure::StaleResourceState: return NavigatorResourceClassification::StaleResourceState;
    case NavigatorResourceLifecycleFailure::AttachmentFailed: return NavigatorResourceClassification::AttachmentFailed;
    case NavigatorResourceLifecycleFailure::ResourceSlotUnavailable: return NavigatorResourceClassification::ResourceSlotUnavailable;
    default: break;
    }
    switch (input.policyFailure) {
    case NavigatorResourcePolicyFailure::UnsupportedScheme: return NavigatorResourceClassification::UnsupportedScheme;
    case NavigatorResourcePolicyFailure::MalformedUrl: return NavigatorResourceClassification::MalformedUrl;
    case NavigatorResourcePolicyFailure::RelativeUrlResolutionFailed: return NavigatorResourceClassification::RelativeUrlResolutionFailed;
    case NavigatorResourcePolicyFailure::CrossOriginPolicyRejected: return NavigatorResourceClassification::CrossOriginPolicyRejected;
    case NavigatorResourcePolicyFailure::ReviewedTargetPolicyRejected: return NavigatorResourceClassification::ReviewedTargetPolicyRejected;
    case NavigatorResourcePolicyFailure::DataUrlUnsupported: return NavigatorResourceClassification::DataUrlUnsupported;
    case NavigatorResourcePolicyFailure::ResourceLimitReached: return NavigatorResourceClassification::ResourceLimitReached;
    case NavigatorResourcePolicyFailure::InsecureRedirectBlocked: return NavigatorResourceClassification::InsecureRedirectBlocked;
    default: break;
    }
    switch (input.transportFailure) {
    case NavigatorResourceTransportFailure::DnsFailed: return NavigatorResourceClassification::DnsFailed;
    case NavigatorResourceTransportFailure::TcpFailed: return NavigatorResourceClassification::TcpFailed;
    case NavigatorResourceTransportFailure::TlsFailed: return NavigatorResourceClassification::TlsFailed;
    case NavigatorResourceTransportFailure::CertificateFailed: return NavigatorResourceClassification::CertificateFailed;
    case NavigatorResourceTransportFailure::HostnameFailed: return NavigatorResourceClassification::HostnameFailed;
    case NavigatorResourceTransportFailure::TimeoutConnect: return NavigatorResourceClassification::TimeoutConnect;
    case NavigatorResourceTransportFailure::TimeoutTls: return NavigatorResourceClassification::TimeoutTls;
    case NavigatorResourceTransportFailure::TimeoutHttp: return NavigatorResourceClassification::TimeoutHttp;
    case NavigatorResourceTransportFailure::ConnectionClosedIncomplete: return NavigatorResourceClassification::ConnectionClosedIncomplete;
    default: break;
    }
    switch (input.httpFailure) {
    case NavigatorResourceHttpFailure::RedirectLimit: return NavigatorResourceClassification::HttpRedirectLimit;
    case NavigatorResourceHttpFailure::RedirectInvalid: return NavigatorResourceClassification::HttpRedirectInvalid;
    case NavigatorResourceHttpFailure::Status3xxUnresolved: return NavigatorResourceClassification::HttpStatus3xxUnresolved;
    case NavigatorResourceHttpFailure::Status4xx: return NavigatorResourceClassification::HttpStatus4xx;
    case NavigatorResourceHttpFailure::Status5xx: return NavigatorResourceClassification::HttpStatus5xx;
    case NavigatorResourceHttpFailure::StatusOther: return NavigatorResourceClassification::HttpStatusOther;
    case NavigatorResourceHttpFailure::BodyTooLargeEncoded: return NavigatorResourceClassification::BodyTooLargeEncoded;
    case NavigatorResourceHttpFailure::MalformedHttp: return NavigatorResourceClassification::MalformedHttp;
    case NavigatorResourceHttpFailure::UnsupportedTransferEncoding: return NavigatorResourceClassification::UnsupportedTransferEncoding;
    default: break;
    }
    switch (input.contentEncodingFailure) {
    case NavigatorResourceContentEncodingFailure::Unsupported: return NavigatorResourceClassification::UnsupportedContentEncoding;
    case NavigatorResourceContentEncodingFailure::MalformedGzip: return NavigatorResourceClassification::MalformedGzip;
    case NavigatorResourceContentEncodingFailure::MalformedDeflate: return NavigatorResourceClassification::MalformedDeflate;
    case NavigatorResourceContentEncodingFailure::DecodedTooLarge: return NavigatorResourceClassification::DecodedResourceTooLarge;
    default: break;
    }
    switch (input.mimeFailure) {
    case NavigatorResourceMimeFailure::UnsupportedMime: return NavigatorResourceClassification::UnsupportedMime;
    case NavigatorResourceMimeFailure::ContentTypeMissing: return NavigatorResourceClassification::ContentTypeMissing;
    case NavigatorResourceMimeFailure::ContentTypeMismatch: return NavigatorResourceClassification::ContentTypeMismatch;
    case NavigatorResourceMimeFailure::MimeExtensionDisagreement: return NavigatorResourceClassification::MimeExtensionDisagreement;
    case NavigatorResourceMimeFailure::UnsupportedSvg: return NavigatorResourceClassification::UnsupportedSvg;
    case NavigatorResourceMimeFailure::UnsupportedWebp: return NavigatorResourceClassification::UnsupportedWebp;
    case NavigatorResourceMimeFailure::UnsupportedAvif: return NavigatorResourceClassification::UnsupportedAvif;
    case NavigatorResourceMimeFailure::UnsupportedGif: return NavigatorResourceClassification::UnsupportedGif;
    case NavigatorResourceMimeFailure::UnsupportedOtherImage: return NavigatorResourceClassification::UnsupportedOtherImage;
    default: break;
    }
    switch (input.decodeFailure) {
    case NavigatorResourceDecodeFailure::PngDecodeFailed: return NavigatorResourceClassification::PngDecodeFailed;
    case NavigatorResourceDecodeFailure::JpegDecodeFailed: return NavigatorResourceClassification::JpegDecodeFailed;
    case NavigatorResourceDecodeFailure::DimensionsTooLarge: return NavigatorResourceClassification::ImageDimensionsTooLarge;
    case NavigatorResourceDecodeFailure::PixelBudgetExceeded: return NavigatorResourceClassification::ImagePixelBudgetExceeded;
    case NavigatorResourceDecodeFailure::AllocationFailed: return NavigatorResourceClassification::ImageAllocationFailed;
    case NavigatorResourceDecodeFailure::UnsupportedJpegVariant: return NavigatorResourceClassification::UnsupportedJpegVariant;
    case NavigatorResourceDecodeFailure::CorruptImage: return NavigatorResourceClassification::CorruptImage;
    default: break;
    }
    if (input.loaded) {
        if (input.imageFormat == NavigatorResourceImageFormat::Png) return NavigatorResourceClassification::LoadedPng;
        if (input.imageFormat == NavigatorResourceImageFormat::Jpeg) return NavigatorResourceClassification::LoadedJpeg;
        return NavigatorResourceClassification::LoadedOtherExistingSupportedResource;
    }
    if (input.httpStatusCode >= 300 && input.httpStatusCode < 400)
        return NavigatorResourceClassification::HttpStatus3xxUnresolved;
    if (input.httpStatusCode >= 400 && input.httpStatusCode < 500)
        return NavigatorResourceClassification::HttpStatus4xx;
    if (input.httpStatusCode >= 500 && input.httpStatusCode < 600)
        return NavigatorResourceClassification::HttpStatus5xx;
    return NavigatorResourceClassification::OtherFailure;
}

inline const char* navigatorResourceClassificationName(NavigatorResourceClassification value)
{
    switch (value) {
    case NavigatorResourceClassification::LoadedPng: return "loaded_png";
    case NavigatorResourceClassification::LoadedJpeg: return "loaded_jpeg";
    case NavigatorResourceClassification::LoadedOtherExistingSupportedResource: return "loaded_other_existing_supported_resource";
    case NavigatorResourceClassification::DnsFailed: return "dns_failed";
    case NavigatorResourceClassification::TcpFailed: return "tcp_failed";
    case NavigatorResourceClassification::TlsFailed: return "tls_failed";
    case NavigatorResourceClassification::CertificateFailed: return "certificate_failed";
    case NavigatorResourceClassification::HostnameFailed: return "hostname_failed";
    case NavigatorResourceClassification::TimeoutConnect: return "timeout_connect";
    case NavigatorResourceClassification::TimeoutTls: return "timeout_tls";
    case NavigatorResourceClassification::TimeoutHttp: return "timeout_http";
    case NavigatorResourceClassification::ConnectionClosedIncomplete: return "connection_closed_incomplete";
    case NavigatorResourceClassification::HttpRedirectLimit: return "http_redirect_limit";
    case NavigatorResourceClassification::HttpRedirectInvalid: return "http_redirect_invalid";
    case NavigatorResourceClassification::HttpStatus3xxUnresolved: return "http_status_3xx_unresolved";
    case NavigatorResourceClassification::HttpStatus4xx: return "http_status_4xx";
    case NavigatorResourceClassification::HttpStatus5xx: return "http_status_5xx";
    case NavigatorResourceClassification::HttpStatusOther: return "http_status_other";
    case NavigatorResourceClassification::BodyTooLargeEncoded: return "body_too_large_encoded";
    case NavigatorResourceClassification::MalformedHttp: return "malformed_http";
    case NavigatorResourceClassification::UnsupportedTransferEncoding: return "unsupported_transfer_encoding";
    case NavigatorResourceClassification::UnsupportedContentEncoding: return "unsupported_content_encoding";
    case NavigatorResourceClassification::MalformedGzip: return "malformed_gzip";
    case NavigatorResourceClassification::MalformedDeflate: return "malformed_deflate";
    case NavigatorResourceClassification::DecodedResourceTooLarge: return "decoded_resource_too_large";
    case NavigatorResourceClassification::UnsupportedMime: return "unsupported_mime";
    case NavigatorResourceClassification::ContentTypeMissing: return "content_type_missing";
    case NavigatorResourceClassification::ContentTypeMismatch: return "content_type_mismatch";
    case NavigatorResourceClassification::MimeExtensionDisagreement: return "mime_extension_disagreement";
    case NavigatorResourceClassification::UnsupportedSvg: return "unsupported_svg";
    case NavigatorResourceClassification::UnsupportedWebp: return "unsupported_webp";
    case NavigatorResourceClassification::UnsupportedAvif: return "unsupported_avif";
    case NavigatorResourceClassification::UnsupportedGif: return "unsupported_gif";
    case NavigatorResourceClassification::UnsupportedOtherImage: return "unsupported_other_image";
    case NavigatorResourceClassification::UnsupportedScheme: return "unsupported_scheme";
    case NavigatorResourceClassification::MalformedUrl: return "malformed_url";
    case NavigatorResourceClassification::RelativeUrlResolutionFailed: return "relative_url_resolution_failed";
    case NavigatorResourceClassification::CrossOriginPolicyRejected: return "cross_origin_policy_rejected";
    case NavigatorResourceClassification::ReviewedTargetPolicyRejected: return "reviewed_target_policy_rejected";
    case NavigatorResourceClassification::DataUrlUnsupported: return "data_url_unsupported";
    case NavigatorResourceClassification::ResourceLimitReached: return "resource_limit_reached";
    case NavigatorResourceClassification::InsecureRedirectBlocked: return "insecure_redirect_blocked";
    case NavigatorResourceClassification::PngDecodeFailed: return "png_decode_failed";
    case NavigatorResourceClassification::JpegDecodeFailed: return "jpeg_decode_failed";
    case NavigatorResourceClassification::ImageDimensionsTooLarge: return "image_dimensions_too_large";
    case NavigatorResourceClassification::ImagePixelBudgetExceeded: return "image_pixel_budget_exceeded";
    case NavigatorResourceClassification::ImageAllocationFailed: return "image_allocation_failed";
    case NavigatorResourceClassification::UnsupportedJpegVariant: return "unsupported_jpeg_variant";
    case NavigatorResourceClassification::CorruptImage: return "corrupt_image";
    case NavigatorResourceClassification::DuplicateResourceSkipped: return "duplicate_resource_skipped";
    case NavigatorResourceClassification::StaleResourceState: return "stale_resource_state";
    case NavigatorResourceClassification::AttachmentFailed: return "attachment_failed";
    case NavigatorResourceClassification::ResourceSlotUnavailable: return "resource_slot_unavailable";
    case NavigatorResourceClassification::OtherFailure: return "other_failure";
    }
    return "other_failure";
}

} // namespace apps
} // namespace gxos
