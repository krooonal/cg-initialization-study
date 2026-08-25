#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <variant>
#include <optional>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cctype>
#include <cstdint>
#include <iostream>

namespace cg {

class JsonValue {
public:
    using Object = std::unordered_map<std::string, JsonValue>;
    using Array = std::vector<JsonValue>;
    using Variant = std::variant<std::monostate, bool, int64_t, double, std::string, Object, Array>;

    JsonValue() : value_(std::monostate{}) {}
    JsonValue(std::nullptr_t) : value_(std::monostate{}) {}
    JsonValue(bool v) : value_(v) {}
    JsonValue(int64_t v) : value_(v) {}
    JsonValue(int v) : value_(static_cast<int64_t>(v)) {}
    JsonValue(double v) : value_(v) {}
    JsonValue(const std::string& v) : value_(v) {}
    JsonValue(std::string&& v) : value_(std::move(v)) {}
    JsonValue(const char* v) : value_(std::string(v)) {}
    JsonValue(const Object& v) : value_(v) {}
    JsonValue(Object&& v) : value_(std::move(v)) {}
    JsonValue(const Array& v) : value_(v) {}
    JsonValue(Array&& v) : value_(std::move(v)) {}

    template <typename T>
    bool is() const {
        return std::holds_alternative<T>(value_);
    }

    template <typename T>
    const T& as() const {
        return std::get<T>(value_);
    }

    template <typename T>
    T& as() {
        return std::get<T>(value_);
    }

    bool is_null() const { return is<std::monostate>(); }
    bool is_bool() const { return is<bool>(); }
    bool is_int() const { return is<int64_t>(); }
    bool is_double() const { return is<double>(); }
    bool is_number() const { return is_int() || is_double(); }
    bool is_string() const { return is<std::string>(); }
    bool is_object() const { return is<Object>(); }
    bool is_array() const { return is<Array>(); }

    const Object& as_object() const { return as<Object>(); }
    Object& as_object() { return as<Object>(); }
    const Array& as_array() const { return as<Array>(); }
    Array& as_array() { return as<Array>(); }

    const JsonValue& operator[](const std::string& key) const {
        return as_object().at(key);
    }
    JsonValue& operator[](const std::string& key) {
        if (!is_object()) value_ = Object{};
        return as_object()[key];
    }
    const JsonValue& operator[](size_t idx) const {
        return as_array().at(idx);
    }
    JsonValue& operator[](size_t idx) {
        if (!is_array()) value_ = Array{};
        return as_array()[idx];
    }

    bool has_key(const std::string& key) const {
        if (!is_object()) return false;
        return as_object().find(key) != as_object().end();
    }

    template <typename T>
    T get(const std::string& key, T default_value = T{}) const {
        if (!has_key(key)) return default_value;
        const auto& v = (*this)[key];
        if (v.is_null()) return default_value;
        if constexpr (std::is_same_v<T, bool>) return v.as<bool>();
        else if constexpr (std::is_same_v<T, int>) return static_cast<int>(v.as<int64_t>());
        else if constexpr (std::is_same_v<T, int64_t>) return v.as<int64_t>();
        else if constexpr (std::is_same_v<T, double>) return v.is_int() ? static_cast<double>(v.as<int64_t>()) : v.as<double>();
        else if constexpr (std::is_same_v<T, std::string>) return v.as<std::string>();
        else return default_value;
    }

    std::string dump(int indent = 0) const {
        std::ostringstream oss;
        dump_to_stream(oss, indent);
        return oss.str();
    }

    void dump_to_stream(std::ostream& os, int indent = 0) const {
        std::visit([&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                os << "null";
            } else if constexpr (std::is_same_v<T, bool>) {
                os << (v ? "true" : "false");
            } else if constexpr (std::is_same_v<T, int64_t>) {
                os << v;
            } else if constexpr (std::is_same_v<T, double>) {
                os << v;
            } else if constexpr (std::is_same_v<T, std::string>) {
                os << '"' << escape_string(v) << '"';
            } else if constexpr (std::is_same_v<T, Object>) {
                os << "{\n";
                bool first = true;
                for (const auto& [k, val] : v) {
                    if (!first) os << ",\n";
                    first = false;
                    os << std::string(indent + 2, ' ') << '"' << escape_string(k) << "\": ";
                    val.dump_to_stream(os, indent + 2);
                }
                os << "\n" << std::string(indent, ' ') << "}";
            } else if constexpr (std::is_same_v<T, Array>) {
                os << "[\n";
                for (size_t i = 0; i < v.size(); ++i) {
                    if (i > 0) os << ",\n";
                    os << std::string(indent + 2, ' ');
                    v[i].dump_to_stream(os, indent + 2);
                }
                os << "\n" << std::string(indent, ' ') << "]";
            }
        }, value_);
    }

    static JsonValue parse(const std::string& str) {
        size_t pos = 0;
        return parse_value(str, pos);
    }

    static JsonValue parse_file(const std::string& filepath) {
        std::ifstream file(filepath);
        if (!file) throw std::runtime_error("Cannot open file: " + filepath);
        std::stringstream buffer;
        buffer << file.rdbuf();
        return parse(buffer.str());
    }

    void write_file(const std::string& filepath) const {
        std::ofstream file(filepath);
        if (!file) throw std::runtime_error("Cannot write file: " + filepath);
        dump_to_stream(file);
        file << '\n';
    }

private:
    Variant value_;

    static std::string escape_string(const std::string& s) {
        std::string result;
        result.reserve(s.size() + 4);
        for (char c : s) {
            switch (c) {
                case '"': result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\b': result += "\\b"; break;
                case '\f': result += "\\f"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default:
                    if (static_cast<uint8_t>(c) < 0x20) {
                        char buf[7];
                        snprintf(buf, sizeof(buf), "\\u%04x", static_cast<uint8_t>(c));
                        result += buf;
                    } else {
                        result += c;
                    }
            }
        }
        return result;
    }

    static void skip_whitespace(const std::string& str, size_t& pos) {
        while (pos < str.size() && std::isspace(static_cast<unsigned char>(str[pos]))) ++pos;
    }

    static JsonValue parse_value(const std::string& str, size_t& pos) {
        skip_whitespace(str, pos);
        if (pos >= str.size()) throw std::runtime_error("Unexpected end of input");

        char c = str[pos];
        if (c == 'n') return parse_null(str, pos);
        if (c == 't' || c == 'f') return parse_bool(str, pos);
        if (c == '"') return parse_string(str, pos);
        if (c == '{') return parse_object(str, pos);
        if (c == '[') return parse_array(str, pos);
        return parse_number(str, pos);
    }

    static JsonValue parse_null(const std::string& str, size_t& pos) {
        if (str.substr(pos, 4) != "null") throw std::runtime_error("Expected 'null'");
        pos += 4;
        return JsonValue(nullptr);
    }

    static JsonValue parse_bool(const std::string& str, size_t& pos) {
        if (str.substr(pos, 4) == "true") { pos += 4; return JsonValue(true); }
        if (str.substr(pos, 5) == "false") { pos += 5; return JsonValue(false); }
        throw std::runtime_error("Expected boolean");
    }

    static JsonValue parse_string(const std::string& str, size_t& pos) {
        ++pos;
        std::string result;
        while (pos < str.size()) {
            char c = str[pos++];
            if (c == '"') break;
            if (c == '\\') {
                if (pos >= str.size()) throw std::runtime_error("Incomplete escape sequence");
                char esc = str[pos++];
                switch (esc) {
                    case '"': result += '"'; break;
                    case '\\': result += '\\'; break;
                    case '/': result += '/'; break;
                    case 'b': result += '\b'; break;
                    case 'f': result += '\f'; break;
                    case 'n': result += '\n'; break;
                    case 'r': result += '\r'; break;
                    case 't': result += '\t'; break;
                    case 'u': {
                        if (pos + 4 > str.size()) throw std::runtime_error("Incomplete unicode escape");
                        std::string hex = str.substr(pos, 4);
                        pos += 4;
                        uint32_t codepoint = std::stoul(hex, nullptr, 16);
                        if (codepoint <= 0x7F) result += static_cast<char>(codepoint);
                        else if (codepoint <= 0x7FF) {
                            result += static_cast<char>(0xC0 | (codepoint >> 6));
                            result += static_cast<char>(0x80 | (codepoint & 0x3F));
                        } else {
                            result += static_cast<char>(0xE0 | (codepoint >> 12));
                            result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                            result += static_cast<char>(0x80 | (codepoint & 0x3F));
                        }
                        break;
                    }
                    default: throw std::runtime_error("Invalid escape sequence");
                }
            } else {
                result += c;
            }
        }
        return JsonValue(result);
    }

    static JsonValue parse_number(const std::string& str, size_t& pos) {
        size_t start = pos;
        bool has_dot = false;
        if (str[pos] == '-') ++pos;
        while (pos < str.size() && std::isdigit(static_cast<unsigned char>(str[pos]))) ++pos;
        if (pos < str.size() && str[pos] == '.') { has_dot = true; ++pos; }
        while (pos < str.size() && std::isdigit(static_cast<unsigned char>(str[pos]))) ++pos;
        if (pos < str.size() && (str[pos] == 'e' || str[pos] == 'E')) {
            ++pos;
            if (pos < str.size() && (str[pos] == '+' || str[pos] == '-')) ++pos;
            while (pos < str.size() && std::isdigit(static_cast<unsigned char>(str[pos]))) ++pos;
        }
        std::string num_str = str.substr(start, pos - start);
        if (has_dot) return JsonValue(std::stod(num_str));
        return JsonValue(static_cast<int64_t>(std::stoll(num_str)));
    }

    static JsonValue parse_object(const std::string& str, size_t& pos) {
        ++pos;
        Object obj;
        skip_whitespace(str, pos);
        if (pos < str.size() && str[pos] == '}') { ++pos; return JsonValue(std::move(obj)); }
        while (true) {
            skip_whitespace(str, pos);
            JsonValue key = parse_string(str, pos);
            skip_whitespace(str, pos);
            if (pos >= str.size() || str[pos] != ':') throw std::runtime_error("Expected ':'");
            ++pos;
            JsonValue value = parse_value(str, pos);
            obj.emplace(key.as<std::string>(), std::move(value));
            skip_whitespace(str, pos);
            if (pos >= str.size()) throw std::runtime_error("Unexpected end in object");
            if (str[pos] == '}') { ++pos; break; }
            if (str[pos] != ',') throw std::runtime_error("Expected ',' or '}'");
            ++pos;
        }
        return JsonValue(std::move(obj));
    }

    static JsonValue parse_array(const std::string& str, size_t& pos) {
        ++pos;
        Array arr;
        skip_whitespace(str, pos);
        if (pos < str.size() && str[pos] == ']') { ++pos; return JsonValue(std::move(arr)); }
        while (true) {
            arr.push_back(parse_value(str, pos));
            skip_whitespace(str, pos);
            if (pos >= str.size()) throw std::runtime_error("Unexpected end in array");
            if (str[pos] == ']') { ++pos; break; }
            if (str[pos] != ',') throw std::runtime_error("Expected ',' or ']'");
            ++pos;
        }
        return JsonValue(std::move(arr));
    }
};

inline std::ostream& operator<<(std::ostream& os, const JsonValue& v) {
    v.dump_to_stream(os);
    return os;
}

} // namespace cg