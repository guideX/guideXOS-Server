#include "navigator_javascript/json_serializer.h"

using namespace gxos::javascript;

extern "C" std::uint32_t gxos_js124_json_serializer_probe(
    const Value* input, PortableObjectStore* objects,
    RuntimeStringStore* strings, char* outputBuffer,
    std::size_t outputCapacity, PortableJsonTraversalFrame* frames,
    std::size_t frameCapacity, char* numberScratch,
    std::size_t numberScratchCapacity, std::size_t* outputLength,
    std::uint8_t* status)
{
    if (outputLength != nullptr) *outputLength = 0u;
    if (status != nullptr) *status = static_cast<std::uint8_t>(
        PortableJsonSerializeStatus::Error);
    if (input == nullptr || objects == nullptr || strings == nullptr ||
        numberScratch == nullptr || numberScratchCapacity == 0u ||
        (outputBuffer == nullptr && outputCapacity != 0u) ||
        (frames == nullptr && frameCapacity != 0u)) return 1u;

    PortableJsonOutputWriter writer(outputBuffer, outputCapacity);
    const PortableJsonSerializeResult result = serializePortableJson(*input,
        *objects, *strings, writer,
        PortableJsonSerializeStorageView{frames, frameCapacity, numberScratch,
            numberScratchCapacity});
    if (status != nullptr)
        *status = static_cast<std::uint8_t>(result.status);
    if (result.status == PortableJsonSerializeStatus::Success &&
        outputLength != nullptr) *outputLength = result.output.length;
    return static_cast<std::uint32_t>(result.error.code);
}
