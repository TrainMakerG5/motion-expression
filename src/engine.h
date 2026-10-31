#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace expression {
inline constexpr std::array<const char*, 10> property_names = {
    "x", "y", "z", "rotationX", "rotationY", "rotation",
    "scaleX", "scaleY", "scaleZ", "opacity"
};

struct Input {
    double time = 0;
    double frame = 0;
    std::array<double, 10> values = {0, 0, 0, 0, 0, 0, 1, 1, 1, 1};
    std::array<double, 3> controls = {0, 0, 0};
    std::string text;
    int color = 0xffffff;
    bool visible = true;
};

struct Result {
    bool ok = false;
    std::array<double, 10> values{};
    std::string error;
    std::string text;
    int color = 0xffffff;
    bool visible = true;
    std::vector<std::string> effects;
};

Result evaluate(std::string_view source, const Input& input);

struct ScalarResult {
    bool ok = false;
    double value = 0;
    std::string error;
};

ScalarResult evaluate_scalar(std::string_view source, double value, double time, double frame);
}
