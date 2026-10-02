#include "../guide_web_html_parser.h"
#include "../guide_web_http.h"
#include "../navigator_file_io.h"
#include "../navigator_local_document.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace gxos { namespace web {
HttpResponse fetchHttpUrl(const std::string& url)
{
	HttpResponse response;
	response.requestedUrl = url;
	response.error = HttpError::NetworkUnavailable;
	return response;
}
} }

namespace {
int checks = 0;
int failures = 0;

void check(bool condition, const std::string& name)
{
	++checks;
	if (condition) std::cout << "PASS: " << name << "\n";
	else { ++failures; std::cout << "FAIL: " << name << "\n"; }
}

void writeBytes(const std::filesystem::path& path, const std::string& bytes)
{
	std::filesystem::create_directories(path.parent_path());
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}
}

int main()
{
	using namespace gxos::apps;
	const std::filesystem::path root = std::filesystem::current_path();
	const std::filesystem::path fixtureRoot = root / "tmp" /
		("phase12-navigator-local-" + std::to_string(
			std::chrono::steady_clock::now().time_since_epoch().count()));
	std::error_code ignored;
	std::filesystem::create_directories(fixtureRoot);

	std::string htmlUrl;
	check(NavigatorLocalHtmlPathToUrl("/docs/nested folder/index.HtMl", htmlUrl) &&
		htmlUrl == "file:///docs/nested%20folder/index.HtMl",
		"Navigator keeps its canonical App ID and encodes VFS spaces in a truthful file URL");
	std::string decodedPath;
	check(NavigatorLocalFileUrlToPath(htmlUrl, decodedPath) &&
		decodedPath == "/docs/nested folder/index.HtMl",
		"local file URL decodes back to the exact guideXOS VFS path");
	check(NavigatorLocalHtmlPathToUrl("/docs/index.HTML", htmlUrl) &&
		NavigatorLocalHtmlPathToUrl("/docs/index.HtMl", htmlUrl) &&
		NavigatorLocalHtmlPathToUrl("/docs/index.htm", htmlUrl),
		".html and .htm activation paths accept mixed-case extensions");
	check(!NavigatorLocalHtmlPathToUrl("/docs/image.bin", htmlUrl) &&
		!NavigatorLocalHtmlPathToUrl("/../../outside.html", htmlUrl) &&
		!NavigatorLocalHtmlPathToUrl("/docs/" + std::string(kAppModelMaxDocumentPathBytes, 'x') + ".html", htmlUrl),
		"unsupported, root-escaping, and overlong activation paths fail closed");
	check(!NavigatorLocalFileUrlToPath("file://remote-host/docs/page.html", decodedPath) &&
		!NavigatorLocalFileUrlToPath("file:///docs/%00page.html", decodedPath) &&
		!NavigatorLocalFileUrlToPath("file:///docs/%5C%5Coutside.html", decodedPath),
		"local URL parser rejects an authority, decoded controls, and host-style path separators");
	check(!NavigatorMayFollowLocalLink("https://example.test/page.html", "file:///docs/page.html") &&
		NavigatorMayFollowLocalLink("file:///docs/page.html", "https://example.test/") &&
		NavigatorMayFollowLocalLink("file:///docs/page.html", "file:///docs/next.html"),
		"remote-to-local links fail closed while local-to-remote and local relative navigation remain allowed");
	check(!NavigatorMayLoadLocalResource("https://example.test/page.html", "file:///docs/image.png") &&
		NavigatorMayLoadLocalResource("file:///docs/page.html", "file:///docs/image.png"),
		"remote documents cannot load local images while local documents can");

	const std::filesystem::path nestedDir = fixtureRoot / "nested folder";
	const std::filesystem::path emptyHost = nestedDir / "empty.html";
	const std::filesystem::path minimalHost = nestedDir / "minimal.HTML";
	const std::filesystem::path malformedHost = nestedDir / "malformed.htm";
	const std::string minimal = "<!doctype html>\n<html><head><title>Minimal</title></head><body>Hello guideXOS</body></html>";
	writeBytes(emptyHost, "");
	writeBytes(minimalHost, minimal);
	writeBytes(malformedHost, "<html><body><p>Broken <b>but safe");

	auto vfsPath = [&](const std::filesystem::path& host) {
		return std::string("/") + std::filesystem::relative(host, root).generic_string();
	};
	const FileReadResult emptyRead = gxos::apps::readTextFile(vfsPath(emptyHost));
	check(emptyRead.status == FileReadStatus::Ok && emptyRead.text.empty(),
		"hosted VFS adapter reads a zero-byte local HTML document without error");
	const FileReadResult minimalRead = gxos::apps::readTextFile(vfsPath(minimalHost));
	const std::string minimalUrl = "file:///" + std::filesystem::relative(minimalHost, root).generic_string();
	const gxos::web::WebDocument minimalDoc = gxos::web::parseHtml(minimalUrl, minimalRead.text);
	check(minimalRead.status == FileReadStatus::Ok && minimalRead.text == minimal &&
		minimalDoc.url == minimalUrl && minimalDoc.title == "Minimal" &&
		!minimalDoc.blocks.empty() && minimalDoc.blocks.back().text == "Hello guideXOS",
		"minimal local HTML preserves exact source bytes and uses the production HTML parser/title/text model");
	const FileReadResult malformedRead = gxos::apps::readTextFile(vfsPath(malformedHost));
	const gxos::web::WebDocument malformedDoc = gxos::web::parseHtml(
		"file:///tmp/malformed.htm", malformedRead.text);
	check(malformedRead.status == FileReadStatus::Ok && !malformedDoc.url.empty(),
		"malformed local HTML returns a usable production-parser document");
	const gxos::web::WebDocument emptyDoc = gxos::web::parseHtml("file:///tmp/empty.html", emptyRead.text);
	check(emptyDoc.url == "file:///tmp/empty.html" && emptyDoc.blocks.empty(),
		"empty local HTML remains an empty parser document");

	std::string docsPath = "/docs/index.html";
	const FileReadResult fixtureRead = gxos::apps::readTextFile(docsPath);
	const gxos::web::WebDocument fixtureDoc = gxos::web::parseHtml("file:///docs/index.html", fixtureRead.text);
	check(fixtureRead.status == FileReadStatus::Ok && fixtureRead.text.size() > 1000 &&
		fixtureDoc.title == "guideXOS Navigator Help" && fixtureDoc.blocks.size() > 10 &&
		fixtureDoc.cssDiagnostics.styleBlockCount > 0 && fixtureDoc.cssDiagnostics.cssDetected,
		"existing nontrivial docs/index.html loads through the hosted VFS adapter and production parser/CSS pipeline");
	const gxos::web::WebDocument scriptDoc = gxos::web::parseHtml("file:///docs/script.html",
		"<script>document.cookie='must-not-run';</script><p>Static local content</p>");
	bool scriptTextPresent = false;
	bool staticTextPresent = false;
	for (const gxos::web::DocBlock& block : scriptDoc.blocks) {
		scriptTextPresent = scriptTextPresent || block.text.find("must-not-run") != std::string::npos;
		staticTextPresent = staticTextPresent || block.text.find("Static local content") != std::string::npos;
	}
	check(!scriptTextPresent && staticTextPresent,
		"local documents use the existing parser with no JavaScript execution engine or script text privilege");

	const std::string styled = "<html><head><style>p { color: #123456; }</style></head>"
		"<body><p style=\"background-color: #abcdef\">Styled local page</p></body></html>";
	const gxos::web::WebDocument styledDoc = gxos::web::parseHtml("file:///docs/styled.html", styled);
	check(styledDoc.cssDiagnostics.cssDetected && styledDoc.cssDiagnostics.styleBlockCount == 1 &&
		styledDoc.cssDiagnostics.inlineStyleCount == 1,
		"local inline style blocks and style attributes use the existing CSS parser");
	const std::string hostedInlineFixture =
		"<html><body style=\"background:#f7f3e8;color:#1f2937;font-size:18px;line-height:1.5;\">"
		"<h1 style=\"color:#9a3412;text-align:center;margin:12px 0;\">Inline CSS Heading</h1>"
		"<p style=\"background:#dbeafe;padding:8px;margin:12px 0;\">Inline CSS paragraph</p>"
		"<a href=\"#\" style=\"color:#1d4ed8;\">Inline CSS link</a></body></html>";
	const gxos::web::WebDocument hostedInlineDoc = gxos::web::parseHtml(
		"file:///docs/css-inline.html", hostedInlineFixture);
	bool hostedInlineTextComplete = true;
	for (const char* expected : {"Inline CSS Heading", "Inline CSS paragraph", "Inline CSS link"}) {
		bool found = false;
		for (const gxos::web::DocBlock& block : hostedInlineDoc.blocks)
			found = found || block.text.find(expected) != std::string::npos;
		hostedInlineTextComplete = hostedInlineTextComplete && found;
	}
	check(hostedInlineTextComplete && hostedInlineDoc.cssDiagnostics.cssDetected &&
		hostedInlineDoc.cssDiagnostics.styleBlockCount == 0 &&
		hostedInlineDoc.cssDiagnostics.inlineStyleCount >= 4,
		"existing hosted inline-CSS regression fixture survives the production parser with its text and four style attributes");
	const gxos::web::WebDocument imageDoc = gxos::web::parseHtml(
		"file:///docs/nested/index.html", "<img src=\"image.png\" alt=\"fixture\">");
	check(!imageDoc.blocks.empty() && imageDoc.blocks.front().type == gxos::web::BlockType::Image &&
		imageDoc.blocks.front().url == "file:///docs/nested/image.png",
		"local image references resolve against the local document URL");

	const std::string nestedBase = "file:///docs/nested/index.html";
	check(gxos::web::resolveRelativeUrl(nestedBase, "second.html") == "file:///docs/nested/second.html" &&
		gxos::web::resolveRelativeUrl(nestedBase, "../other/page.html") == "file:///docs/other/page.html" &&
		gxos::web::resolveRelativeUrl(nestedBase, "../../outside.html") == "file:///outside.html",
		"relative and parent-relative local navigation normalize without escaping VFS root");

	const std::filesystem::path missingHost = nestedDir / "missing.html";
	const std::filesystem::path oversizedHost = fixtureRoot / "oversized.html";
	const std::filesystem::path maximumHost = fixtureRoot / "maximum.html";
	writeBytes(maximumHost, std::string(gxos::apps::kNavigatorMaxFileBytes, 'm'));
	writeBytes(oversizedHost, std::string(gxos::apps::kNavigatorMaxFileBytes + 1, 'o'));
	check(gxos::apps::readTextFile(vfsPath(missingHost)).status == FileReadStatus::NotFound &&
		gxos::apps::readTextFile(vfsPath(maximumHost)).status == FileReadStatus::Ok &&
		gxos::apps::readTextFile(vfsPath(maximumHost)).text.size() == gxos::apps::kNavigatorMaxFileBytes &&
		gxos::apps::readTextFile(vfsPath(oversizedHost)).status == FileReadStatus::TooLarge,
		"missing files fail safely and local HTML reads enforce the exact 64 KiB source bound");
	check(gxos::apps::readTextFile("/../../outside.html").status == FileReadStatus::IoError &&
		gxos::apps::readTextFile("/docs\\..\\outside.html").status == FileReadStatus::IoError &&
		!gxos::apps::fileExists("/../../outside.html"),
		"hosted VFS translation rejects root escape and host-style path separators");

	std::filesystem::remove_all(fixtureRoot, ignored);
	std::cout << "navigatorLocalDocumentChecks=" << (checks - failures) << "/" << checks << "\n";
	return failures == 0 ? 0 : 1;
}
