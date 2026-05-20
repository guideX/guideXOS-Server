#pragma once
// guide_web_document.h
//
// Reusable guideWeb document model.
//
// Defines the core data types shared by any component that produces or
// consumes parsed web documents: the HTML parser, Navigator, the future Help
// Viewer, HTML e-mail preview, Control Panel rich pages, etc.
//
// This header has no dependency on navigator.h or any Navigator-specific type.
// It depends only on the C++ standard library.

#include <cstdint>
#include <string>
#include <vector>

namespace gxos {
namespace web {

// =============================================================================
// Document model
// =============================================================================

enum class BlockType : uint8_t {
	Heading   = 0,   // large / bold heading
	Paragraph = 1,   // body-text paragraph
	Link      = 2,   // inline link-style text; url field carries the destination
};

struct DocBlock {
	BlockType   type;
	std::string text;
	std::string url;   // only meaningful for BlockType::Link
};

struct WebDocument {
	std::string           url;
	std::string           title;
	std::vector<DocBlock> blocks;
};

} // namespace web
} // namespace gxos
