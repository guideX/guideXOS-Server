#include "window_bounds_store.h"

#include "fs.h"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <sstream>
#include <vector>

namespace gxos { namespace gui {

    namespace {
        constexpr const char* kStorePath = "window-bounds.cfg";
        constexpr size_t kMaxRecords = 64;
        std::mutex s_storeMutex;

        struct Record {
            std::string key;
            NormalWindowBounds bounds;
        };

        bool validKey(const std::string& key) {
            if (key.empty() || key.size() > 180) return false;
            return std::all_of(key.begin(), key.end(), [](unsigned char c) {
                return c >= 0x20 && c != '|' && c != '\r' && c != '\n';
            });
        }

        bool validBounds(const NormalWindowBounds& bounds) {
            return bounds.w > 0 && bounds.h > 0 && bounds.w <= 10000 && bounds.h <= 10000 &&
                bounds.x >= -10000 && bounds.x <= 10000 && bounds.y >= -10000 && bounds.y <= 10000;
        }

        std::vector<Record> loadRecords() {
            std::vector<uint8_t> bytes;
            if (!FS::readAll(kStorePath, bytes, 64 * 1024).success) return {};
            std::istringstream input(std::string(bytes.begin(), bytes.end()));
            std::vector<Record> records;
            std::string line;
            while (records.size() < kMaxRecords && std::getline(input, line)) {
                std::istringstream fields(line);
                Record record;
                std::string xs, ys, ws, hs;
                if (!std::getline(fields, record.key, '|') ||
                    !std::getline(fields, xs, '|') || !std::getline(fields, ys, '|') ||
                    !std::getline(fields, ws, '|') || !std::getline(fields, hs) ||
                    !validKey(record.key)) continue;
                try {
                    record.bounds.x = std::stoi(xs);
                    record.bounds.y = std::stoi(ys);
                    record.bounds.w = std::stoi(ws);
                    record.bounds.h = std::stoi(hs);
                } catch (...) {
                    continue;
                }
                if (validBounds(record.bounds)) records.push_back(record);
            }
            return records;
        }

        bool saveRecords(const std::vector<Record>& records, std::string& error) {
            std::ostringstream output;
            for (const Record& record : records) {
                output << record.key << '|' << record.bounds.x << '|' << record.bounds.y << '|'
                    << record.bounds.w << '|' << record.bounds.h << '\n';
            }
            const std::string serialized = output.str();
            const std::vector<uint8_t> bytes(serialized.begin(), serialized.end());
            if (!FS::writeAll(kStorePath, bytes)) {
                error = "unable to write window-bounds.cfg";
                return false;
            }
            return true;
        }
    }

    bool WindowBoundsStore::Load(const std::string& key, NormalWindowBounds& out) {
        if (!validKey(key)) return false;
        std::lock_guard<std::mutex> lock(s_storeMutex);
        const std::vector<Record> records = loadRecords();
        for (const Record& record : records) {
            if (record.key == key) {
                out = record.bounds;
                return true;
            }
        }
        return false;
    }

    bool WindowBoundsStore::Save(const std::string& key, const NormalWindowBounds& bounds, std::string& error) {
        error.clear();
        if (!validKey(key) || !validBounds(bounds)) {
            error = "invalid window bounds record";
            return false;
        }
        std::lock_guard<std::mutex> lock(s_storeMutex);
        std::vector<Record> records = loadRecords();
        auto it = std::find_if(records.begin(), records.end(), [&](const Record& record) { return record.key == key; });
        if (it != records.end()) {
            it->bounds = bounds;
        } else {
            if (records.size() >= kMaxRecords) records.erase(records.begin());
            records.push_back({key, bounds});
        }
        return saveRecords(records, error);
    }

}} // namespace gxos::gui
