#include "app_manifest_loader.h"

#include "app_manifest_validator.h"

#include <cctype>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>

namespace gxos {
namespace apps {
namespace {

class JsonValue {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolValue = false;
    double numberValue = 0;
    std::string stringValue;
    std::vector<JsonValue> arrayValue;
    std::map<std::string, JsonValue> objectValue;

    bool isObject() const { return type == Type::Object; }
    bool isArray() const { return type == Type::Array; }
    bool isString() const { return type == Type::String; }
    bool isNumber() const { return type == Type::Number; }
    bool isBool() const { return type == Type::Bool; }

    const JsonValue* get(const std::string& name) const {
        auto it = objectValue.find(name);
        return it == objectValue.end() ? nullptr : &it->second;
    }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& input) : m_input(input) {}

    JsonValue parse() {
        skipWhitespace();
        JsonValue value = parseValue();
        skipWhitespace();
        if (!eof()) throw std::runtime_error("Unexpected trailing data in JSON.");
        return value;
    }

private:
    const std::string& m_input;
    size_t m_pos = 0;

    bool eof() const { return m_pos >= m_input.size(); }

    char peek() const {
        if (eof()) throw std::runtime_error("Unexpected end of JSON.");
        return m_input[m_pos];
    }

    char consume() {
        char c = peek();
        ++m_pos;
        return c;
    }

    void skipWhitespace() {
        while (!eof() && std::isspace(static_cast<unsigned char>(m_input[m_pos]))) ++m_pos;
    }

    void expect(char expected) {
        if (consume() != expected) throw std::runtime_error(std::string("Expected '") + expected + "'.");
    }

    JsonValue parseValue() {
        skipWhitespace();
        char c = peek();
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == '"') return parseStringValue();
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parseNumber();
        if (matchLiteral("true")) {
            JsonValue value;
            value.type = JsonValue::Type::Bool;
            value.boolValue = true;
            return value;
        }
        if (matchLiteral("false")) {
            JsonValue value;
            value.type = JsonValue::Type::Bool;
            value.boolValue = false;
            return value;
        }
        if (matchLiteral("null")) return JsonValue();
        throw std::runtime_error("Invalid JSON value.");
    }

    bool matchLiteral(const char* literal) {
        size_t len = std::string(literal).size();
        if (m_input.compare(m_pos, len, literal) != 0) return false;
        m_pos += len;
        return true;
    }

    JsonValue parseObject() {
        JsonValue value;
        value.type = JsonValue::Type::Object;
        expect('{');
        skipWhitespace();
        if (!eof() && peek() == '}') {
            consume();
            return value;
        }
        while (true) {
            skipWhitespace();
            if (peek() != '"') throw std::runtime_error("Expected object property name.");
            std::string name = parseString();
            skipWhitespace();
            expect(':');
            value.objectValue[name] = parseValue();
            skipWhitespace();
            char c = consume();
            if (c == '}') break;
            if (c != ',') throw std::runtime_error("Expected ',' or '}' in object.");
        }
        return value;
    }

    JsonValue parseArray() {
        JsonValue value;
        value.type = JsonValue::Type::Array;
        expect('[');
        skipWhitespace();
        if (!eof() && peek() == ']') {
            consume();
            return value;
        }
        while (true) {
            value.arrayValue.push_back(parseValue());
            skipWhitespace();
            char c = consume();
            if (c == ']') break;
            if (c != ',') throw std::runtime_error("Expected ',' or ']' in array.");
        }
        return value;
    }

    JsonValue parseStringValue() {
        JsonValue value;
        value.type = JsonValue::Type::String;
        value.stringValue = parseString();
        return value;
    }

    std::string parseString() {
        expect('"');
        std::string out;
        while (true) {
            if (eof()) throw std::runtime_error("Unterminated string.");
            char c = consume();
            if (c == '"') break;
            if (c == '\\') {
                if (eof()) throw std::runtime_error("Unterminated escape sequence.");
                char esc = consume();
                switch (esc) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u':
                    for (int i = 0; i < 4; ++i) {
                        if (eof() || !std::isxdigit(static_cast<unsigned char>(consume()))) {
                            throw std::runtime_error("Invalid unicode escape.");
                        }
                    }
                    out.push_back('?');
                    break;
                default:
                    throw std::runtime_error("Invalid escape sequence.");
                }
            } else {
                out.push_back(c);
            }
        }
        return out;
    }

    JsonValue parseNumber() {
        size_t start = m_pos;
        if (!eof() && peek() == '-') consume();
        while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) consume();
        if (!eof() && peek() == '.') {
            consume();
            while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) consume();
        }
        if (!eof() && (peek() == 'e' || peek() == 'E')) {
            consume();
            if (!eof() && (peek() == '+' || peek() == '-')) consume();
            while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) consume();
        }

        JsonValue value;
        value.type = JsonValue::Type::Number;
        value.numberValue = std::stod(m_input.substr(start, m_pos - start));
        return value;
    }
};

std::string stringProperty(const JsonValue& object, const std::string& name) {
    const JsonValue* value = object.get(name);
    return value && value->isString() ? value->stringValue : std::string();
}

int intProperty(const JsonValue& object, const std::string& name, int defaultValue) {
    const JsonValue* value = object.get(name);
    return value && value->isNumber() ? static_cast<int>(value->numberValue) : defaultValue;
}

bool boolProperty(const JsonValue& object, const std::string& name, bool defaultValue) {
    const JsonValue* value = object.get(name);
    return value && value->isBool() ? value->boolValue : defaultValue;
}

std::vector<std::string> stringArrayProperty(const JsonValue& object, const std::string& name) {
    std::vector<std::string> values;
    const JsonValue* array = object.get(name);
    if (!array || !array->isArray()) return values;
    for (const JsonValue& item : array->arrayValue) {
        if (item.isString()) values.push_back(item.stringValue);
    }
    return values;
}

void populateEntries(const JsonValue& root, AppManifest& manifest) {
    const JsonValue* entries = root.get("entries");
    if (!entries || !entries->isArray()) return;
    for (const JsonValue& item : entries->arrayValue) {
        if (!item.isObject()) continue;
        AppEntry entry;
        entry.architecture = stringProperty(item, "architecture");
        entry.path = stringProperty(item, "path");
        entry.entryPoint = stringProperty(item, "entryPoint");
        entry.abi = stringProperty(item, "abi");
        entry.runtime = stringProperty(item, "runtime");
        manifest.entries.push_back(entry);
    }
}

void populateFileAssociations(const JsonValue& root, AppManifest& manifest) {
    const JsonValue* associations = root.get("fileAssociations");
    if (!associations || !associations->isArray()) return;
    for (const JsonValue& item : associations->arrayValue) {
        if (!item.isObject()) continue;
        FileAssociation association;
        association.extension = stringProperty(item, "extension");
        association.contentType = stringProperty(item, "contentType");
        association.description = stringProperty(item, "description");
        manifest.fileAssociations.push_back(association);
    }
}

void populateDefaultWindow(const JsonValue& root, AppManifest& manifest) {
    const JsonValue* window = root.get("defaultWindow");
    if (!window || !window->isObject()) return;
    manifest.defaultWindow.width = intProperty(*window, "width", manifest.defaultWindow.width);
    manifest.defaultWindow.height = intProperty(*window, "height", manifest.defaultWindow.height);
    manifest.defaultWindow.resizable = boolProperty(*window, "resizable", manifest.defaultWindow.resizable);
}

void populateDesktopRegistryHints(const JsonValue& root, AppManifest& manifest) {
    const JsonValue* hints = root.get("desktopRegistryHints");
    if (!hints || !hints->isObject()) return;
    for (const auto& pair : hints->objectValue) {
        if (pair.second.isString()) manifest.desktopRegistryHints[pair.first] = pair.second.stringValue;
    }
}

AppManifest manifestFromJson(const JsonValue& root) {
    AppManifest manifest;
    manifest.schemaVersion = intProperty(root, "schemaVersion", kSupportedAppManifestSchemaVersion);
    manifest.id = stringProperty(root, "id");
    manifest.displayName = stringProperty(root, "displayName");
    manifest.version = stringProperty(root, "version");
    manifest.publisher = stringProperty(root, "publisher");
    manifest.description = stringProperty(root, "description");
    manifest.category = stringProperty(root, "category");
    manifest.kind = AppKindFromString(stringProperty(root, "kind"));
    manifest.icon = stringProperty(root, "icon");
    manifest.minGuideXOSVersion = stringProperty(root, "minGuideXOSVersion");
    manifest.supportedArchitectures = stringArrayProperty(root, "supportedArchitectures");
    manifest.permissions = stringArrayProperty(root, "permissions");
    populateEntries(root, manifest);
    populateFileAssociations(root, manifest);
    populateDefaultWindow(root, manifest);
    populateDesktopRegistryHints(root, manifest);
    return manifest;
}

} // namespace

AppManifestLoadResult AppManifestLoader::LoadFromFile(const std::filesystem::path& appJsonPath) {
    AppManifestLoadResult result;
    std::ifstream file(appJsonPath);
    if (!file) {
        result.errors.push_back("Unable to open manifest: " + appJsonPath.string());
        return result;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return LoadFromString(buffer.str());
}

AppManifestLoadResult AppManifestLoader::LoadFromDirectory(const std::filesystem::path& appDirectory) {
    return LoadFromFile(appDirectory / "app.json");
}

AppManifestLoadResult AppManifestLoader::LoadFromString(const std::string& jsonText) {
    AppManifestLoadResult result;
    try {
        JsonParser parser(jsonText);
        JsonValue root = parser.parse();
        if (!root.isObject()) {
            result.errors.push_back("Manifest root must be a JSON object.");
            return result;
        }

        result.manifest = manifestFromJson(root);
        AppManifestValidationResult validation = AppManifestValidator::Validate(result.manifest);
        result.valid = validation.valid;
        result.errors = validation.errors;
    } catch (const std::exception& ex) {
        result.errors.push_back(std::string("Malformed manifest JSON: ") + ex.what());
    }
    return result;
}

} // namespace apps
} // namespace gxos
