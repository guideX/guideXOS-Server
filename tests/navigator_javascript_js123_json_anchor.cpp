#include "navigator_javascript/json_writer.h"

using namespace gxos::javascript;

extern "C" std::uint32_t gxos_js123_json_writer_probe(
    const char* input, std::size_t inputLength,
    char* outputBuffer, std::size_t outputCapacity,
    std::size_t* outputLength)
{
    if (outputLength == nullptr) return 1u;
    *outputLength = 0u;
    if ((input == nullptr && inputLength != 0u) ||
        (outputBuffer == nullptr && outputCapacity != 0u)) return 2u;

    PortableJsonOutputWriter writer(outputBuffer, outputCapacity);
    if (!writer.appendQuotedUtf8(SourceView(input, inputLength)))
        return 3u + static_cast<std::uint32_t>(writer.error());
    SourceView output;
    if (!writer.complete(output)) return 8u;
    *outputLength = output.length;
    return 0u;
}
