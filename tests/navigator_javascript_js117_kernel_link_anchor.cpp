// JS117-only linker reachability anchor.
//
// This file is not part of the production kernel source lists and is never
// called during startup. It accepts only caller-owned storage and routes a
// real input through the shared lexer and parser so the kernel candidate must
// retain their entry points and data dependencies.

#include "navigator_javascript/ast.h"
#include "navigator_javascript/frontend_storage.h"
#include "navigator_javascript/lexer.h"
#include "navigator_javascript/parser.h"
#include "navigator_javascript/source.h"

#include <cstddef>
#include <cstdint>

extern "C" std::int32_t gxos_js117_frontend_link_anchor(
    const char* sourceBytes, std::size_t sourceLength,
    void* tokenStorage, std::size_t tokenCapacity,
    void* astNodeStorage, std::size_t astNodeCapacity,
    void* astChildStorage, std::size_t astChildCapacity,
    void* parserScratch, std::size_t parserScratchCapacity)
{
    using namespace gxos::javascript;

    if (sourceBytes == nullptr || tokenStorage == nullptr ||
        astNodeStorage == nullptr || astChildStorage == nullptr ||
        parserScratch == nullptr ||
        reinterpret_cast<std::uintptr_t>(tokenStorage) % alignof(Token) != 0u ||
        reinterpret_cast<std::uintptr_t>(astNodeStorage) % alignof(AstNode) != 0u ||
        reinterpret_cast<std::uintptr_t>(astChildStorage) % alignof(AstNodeId) != 0u) {
        return -1;
    }

    const SourceView source(sourceBytes, sourceLength);
    TokenBuffer tokens{static_cast<Token*>(tokenStorage), tokenCapacity, 0u, 0u};
    const Lexer lexer;
    const LexerResult lexed = lexer.tokenizeInto(source, tokens);
    if (!lexed.succeeded()) {
        return -2 - static_cast<std::int32_t>(lexed.error.code);
    }

    const TokenView tokenView = makeTokenView(source, tokens);
    const AstStorageView astStorage{
        static_cast<AstNode*>(astNodeStorage), astNodeCapacity,
        static_cast<AstNodeId*>(astChildStorage), astChildCapacity};
    Ast ast(source, astStorage);
    FrontendArena scratch(parserScratch, parserScratchCapacity);
    const Parser parser;
    ParserError error;
    if (!parser.parseInto(tokenView, ast, scratch, error)) {
        return -32 - static_cast<std::int32_t>(error.code);
    }
    return 0;
}
