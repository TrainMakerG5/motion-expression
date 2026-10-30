#include "bindings.h"

#include <charconv>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace expression {
namespace {
constexpr size_t maximum_records = 4096;
constexpr size_t maximum_text = 65536;

std::string hex(std::string_view source) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (unsigned char character : source) {
        result += digits[character >> 4];
        result += digits[character & 15];
    }
    return result;
}

std::string unhex(std::string_view source) {
    if (source.size() % 2 || source.size() > maximum_text * 2) {
        throw std::runtime_error("Invalid expression data.");
    }
    std::string result;
    for (size_t i = 0; i < source.size(); i += 2) {
        unsigned int value = 0;
        const auto parsed = std::from_chars(source.data() + i, source.data() + i + 2, value, 16);
        if (parsed.ec != std::errc{} || parsed.ptr != source.data() + i + 2 || value == 0) {
            throw std::runtime_error("Invalid expression data.");
        }
        result += static_cast<char>(value);
    }
    return result;
}

bool number(std::string_view text) {
    double value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && std::isfinite(value);
}
}

int Bindings::add(Binding binding) {
    if (binding.source.size() > maximum_text || binding.original.size() > maximum_text) {
        throw std::runtime_error("Expression is too long.");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (entries_.size() >= maximum_records) { throw std::runtime_error("Expression history is full."); }
    size_t serialized_size = 6 + 32 + 2 * (binding.source.size() + binding.original.size());
    for (const auto& entry : entries_) {
        serialized_size += 32 + 2 * (entry.second.source.size() + entry.second.original.size());
    }
    if (serialized_size > 16 * 1024 * 1024) { throw std::runtime_error("Expression project data is full."); }
    const int id = next_id_++;
    entries_.emplace(id, std::move(binding));
    return id;
}

Binding Bindings::get(int id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto entry = entries_.find(id);
    if (entry == entries_.end()) { throw std::runtime_error("Expression is missing from the project."); }
    return entry->second;
}

void Bindings::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
    next_id_ = 1;
}

std::string Bindings::serialize() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string result = "EXPR2;";
    for (const auto& [id, binding] : entries_) {
        result += std::to_string(id) + ":" + hex(binding.source) + ":" + hex(binding.original) + ";";
    }
    return result;
}

bool Bindings::restore(std::string_view data) {
    try {
        if (data.substr(0, 6) != "EXPR2;" || data.size() > 16 * 1024 * 1024) { return false; }
        std::map<int, Binding> restored;
        int next = 1;
        size_t position = 6;
        while (position < data.size()) {
            const auto end = data.find(';', position);
            if (end == std::string_view::npos) { return false; }
            const auto line = data.substr(position, end - position);
            const auto colon = line.find(':');
            const auto second = line.find(':', colon == std::string_view::npos ? 0 : colon + 1);
            if (colon == std::string_view::npos || second == std::string_view::npos) { return false; }
            int id = 0;
            auto parsed = std::from_chars(line.data(), line.data() + colon, id);
            if (parsed.ec != std::errc{} || parsed.ptr != line.data() + colon || id <= 0 || id > 1000000) { return false; }
            if (!restored.emplace(id, Binding{unhex(line.substr(colon + 1, second - colon - 1)), unhex(line.substr(second + 1))}).second) { return false; }
            if (restored.size() > maximum_records) { return false; }
            next = (std::max)(next, id + 1);
            position = end + 1;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        entries_ = std::move(restored);
        next_id_ = next;
        return true;
    } catch (...) {
        return false;
    }
}

std::string attach_track(std::string_view original, int id, bool enabled) {
    if (id <= 0 || original.empty() || original.size() > maximum_text || original.find('|') != std::string_view::npos) {
        throw std::runtime_error("This movement mode is not supported yet.");
    }
    std::string points(original);
    if (!number(original)) {
        const auto flag = original.rfind(',');
        if (flag == std::string_view::npos || original.substr(flag + 1) != "0") {
            throw std::runtime_error("Acceleration, references and custom movement are not supported yet.");
        }
        const auto mode = original.rfind(',', flag - 1);
        if (mode == std::string_view::npos || original.substr(mode + 1, flag - mode - 1) != "直線移動") {
            throw std::runtime_error("Only stationary values and linear movement are supported yet.");
        }
        points = std::string(original.substr(0, mode));
        std::istringstream values(points);
        std::string item;
        while (std::getline(values, item, ',')) {
            if (!number(item)) { throw std::runtime_error("Invalid track value."); }
        }
    }
    return points + "," + track_mode + ",0|" + std::to_string(id) + (enabled ? ",1" : ",0");
}

int track_binding(std::string_view raw) {
    const std::string marker = std::string(",") + track_mode + ",0|";
    const auto begin = raw.find(marker);
    if (begin == std::string_view::npos) { return 0; }
    int id = 0;
    const auto suffix = raw.substr(begin + marker.size());
    const auto comma = suffix.find(',');
    if (comma == std::string_view::npos || (suffix.substr(comma + 1) != "0" && suffix.substr(comma + 1) != "1")) { return 0; }
    const auto token = suffix.substr(0, comma);
    auto parsed = std::from_chars(token.data(), token.data() + token.size(), id);
    return parsed.ec == std::errc{} && parsed.ptr == token.data() + token.size() && id > 0 ? id : 0;
}

std::string detach_track(std::string_view attached, std::string_view original) {
    const std::string marker = std::string(",") + track_mode + ",0|";
    const auto begin = attached.find(marker);
    if (!track_binding(attached)) { throw std::runtime_error("Invalid expression track."); }
    const auto points = attached.substr(0, begin);
    if (number(original) && points.find(',') == std::string_view::npos) { return std::string(points); }
    return std::string(points) + ",直線移動,0";
}
}
