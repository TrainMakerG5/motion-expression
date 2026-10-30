#pragma once

#include <map>
#include <mutex>
#include <string>
#include <string_view>

namespace expression {
inline constexpr char track_mode[] = "JSエクスプレッション@Expression";

struct Binding {
    std::string source;
    std::string original;
};

class Bindings {
public:
    int add(Binding binding);
    Binding get(int id) const;
    void clear();
    std::string serialize() const;
    bool restore(std::string_view data);
private:
    mutable std::mutex mutex_;
    std::map<int, Binding> entries_;
    int next_id_ = 1;
};

std::string attach_track(std::string_view original, int id, bool enabled = true);
int track_binding(std::string_view raw);
std::string detach_track(std::string_view attached, std::string_view original);
}
