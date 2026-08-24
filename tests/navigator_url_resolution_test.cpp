// Focused regression coverage for Navigator resource and redirect URL joins.

#include "../guide_web_html_parser.h"

#include <cstdio>
#include <string>

static int s_failures = 0;

static void check(const char* label, const std::string& base, const std::string& href,
	const std::string& expected)
{
	const std::string actual = gxos::web::resolveRelativeUrl(base, href);
	if (actual == expected) {
		std::printf("[PASS] %s => %s\n", label, actual.c_str());
		return;
	}
	std::printf("[FAIL] %s\n  base: %s\n  href: %s\n  expected: %s\n  actual: %s\n",
		label, base.c_str(), href.c_str(), expected.c_str(), actual.c_str());
	++s_failures;
}

int main()
{
	const std::string base = "https://example.com/docs/section/index.html?old=1#top";
	check("absolute", base, "https://cdn.example.net/a.png", "https://cdn.example.net/a.png");
	check("protocol-relative", base, "//cdn.example.net/a.png", "https://cdn.example.net/a.png");
	check("root-relative", base, "/images/a.png", "https://example.com/images/a.png");
	check("path-relative", base, "images/a.png", "https://example.com/docs/section/images/a.png");
	check("parent-relative", base, "../images/a.png", "https://example.com/docs/images/a.png");
	check("dot-segments", base, "./../images/./a.png", "https://example.com/docs/images/a.png");
	check("query-only", base, "?v=2", "https://example.com/docs/section/index.html?v=2");
	check("fragment-only", base, "#next", base);
	check("percent-encoding", base, "images/a%20b.png?x=%2F", "https://example.com/docs/section/images/a%20b.png?x=%2F");
	check("file-parent-relative", "file:///docs/section/index.html", "../images/a.png", "file:///docs/images/a.png");
	check("file-root-relative", "file:///docs/section/index.html", "/images/a.png", "file:///images/a.png");

	std::printf("Navigator URL resolution tests: %s\n", s_failures == 0 ? "PASS" : "FAIL");
	return s_failures == 0 ? 0 : 1;
}
