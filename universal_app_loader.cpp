#include "universal_app_loader.h"
#include "gxapp_loader.h"

namespace gxos {

bool UniversalAppLoader::Execute(const std::string& packagePath, std::string& error){
    return GXAppLoader::Execute(packagePath, error);
}

bool UniversalAppLoader::LoadBinary(const std::string& packagePath, std::vector<uint8_t>& binary, std::string& entryPoint, std::string& error){
    return GXAppLoader::LoadBinary(packagePath, binary, entryPoint, error);
}

} // namespace gxos
