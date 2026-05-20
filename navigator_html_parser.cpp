// navigator_html_parser.cpp
//
// Compatibility translation unit for guideXOS Navigator.
//
// The HTML parser implementation now lives in guide_web_html_parser.cpp
// (namespace gxos::web).  navigator_html_parser.h brings the symbols into
// gxos::apps via using-declarations.  This file is kept so that the existing
// build system entry for navigator_html_parser.cpp continues to compile
// without error; it has no definitions of its own.

#include "navigator_html_parser.h"
