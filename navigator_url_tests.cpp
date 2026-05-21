// navigator_url_tests.cpp
//
// Lightweight compile-time + runtime validation for Navigator::normalizeUrl().
// Build and run this file standalone (or include in a future test harness) to
// verify address bar URL normalization without needing the full compositor.
//
// Compile standalone (MSVC example):
//   cl /EHsc /std:c++17 /DGXOS_BARE_METAL=0 navigator_url_tests.cpp
// Run:
//   navigator_url_tests.exe
//
// This file does NOT depend on gui_protocol.h, ipc_bus.h, or any window state.
// It reimplements normalizeUrl() inline so the logic can be tested in isolation.
// Caret / editing state is not covered here; it is pure integer bookkeeping.

#include <cassert>
#include <cstdio>
#include <string>

// ---------------------------------------------------------------------------
// Mirror of Navigator::normalizeUrl() – must stay in sync with navigator.cpp
// ---------------------------------------------------------------------------
static std::string normalizeUrl(const std::string& input)
{
	if (input.empty()) return input;

	// Already has any scheme (file://, http://, https://, about:, etc.) – pass through.
	if (input.find("://") != std::string::npos) return input;
	if (input.size() >= 6 && input.substr(0, 6) == "about:") return input;

	// Bare path – convert to a file:// URL producing exactly three leading slashes.
	std::string path = input;
	size_t firstNonSlash = path.find_first_not_of('/');
	if (firstNonSlash == std::string::npos) return "file:///";
	path = path.substr(firstNonSlash);
	return std::string("file:///") + path;
}

// ---------------------------------------------------------------------------
// Test harness
// ---------------------------------------------------------------------------
static int s_pass = 0;
static int s_fail = 0;

static void check(const char* label, const std::string& input, const std::string& expected)
{
	std::string result = normalizeUrl(input);
	if (result == expected) {
		std::printf("[PASS] %-40s  =>  %s\n", label, result.c_str());
		++s_pass;
	} else {
		std::printf("[FAIL] %-40s\n"
					"       input    : %s\n"
					"       expected : %s\n"
					"       got      : %s\n",
					label, input.c_str(), expected.c_str(), result.c_str());
		++s_fail;
	}
}

int main()
{
	std::printf("=== Navigator normalizeUrl() tests ===\n\n");

	// --- Pass-through cases ---
	check("about:navigator",        "about:navigator",        "about:navigator");
	check("about:bookmarks",        "about:bookmarks",        "about:bookmarks");
	check("file:///docs/index.html","file:///docs/index.html","file:///docs/index.html");
	check("http://example.com/",    "http://example.com/",    "http://example.com/");
	check("https://example.com/",   "https://example.com/",   "https://example.com/");

	// --- Path normalisation ---
	check("/docs/index.html",       "/docs/index.html",       "file:///docs/index.html");
	check("docs/index.html",        "docs/index.html",        "file:///docs/index.html");
	check("//docs/index.html",      "//docs/index.html",      "file:///docs/index.html");
	check("/",                      "/",                      "file:///");
	check("",                       "",                       "");

	// --- Edge cases ---
	check("about:unknown",          "about:unknown",          "about:unknown");
	check("/docs/sub/page.html",    "/docs/sub/page.html",    "file:///docs/sub/page.html");
	check("docs/sub/page.html",     "docs/sub/page.html",     "file:///docs/sub/page.html");

	std::printf("\n=== Results: %d passed, %d failed ===\n", s_pass, s_fail);
	return s_fail == 0 ? 0 : 1;
}
