#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace xdcam {

class JsonError : public std::runtime_error {
   public:
    explicit JsonError(const std::string& message) : std::runtime_error(message) {}
};

struct Value {
    enum class Type { Null, Bool, Int, String, Array, Object };

    Type type = Type::Null;
    bool b = false;
    int64_t i = 0;
    std::string s;
    std::vector<Value> a;
    std::vector<std::pair<std::string, Value>> o;

    const Value* Find(std::string_view key) const {
        for (const auto& entry : o) {
            if (entry.first == key) {
                return &entry.second;
            }
        }
        return nullptr;
    }

    bool Has(std::string_view key) const { return Find(key) != nullptr; }
};

Value ParseJson(std::string_view text);

}  // namespace xdcam
