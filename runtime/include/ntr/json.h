// A small JSON reader and writer for the project's own files (the achievements' definitions
// and each save's achievements). Objects keep their keys in the order read or set, so a
// file written back keeps its order.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace ntr {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool b) : type_(Type::Bool), bool_(b) {}
    Json(double n) : type_(Type::Number), number_(n) {}
    Json(int n) : type_(Type::Number), number_(n) {}
    Json(const char* s) : type_(Type::String), string_(s) {}
    Json(std::string s) : type_(Type::String), string_(std::move(s)) {}
    static Json array() { Json j; j.type_ = Type::Array; return j; }
    static Json object() { Json j; j.type_ = Type::Object; return j; }

    Type type() const { return type_; }
    bool is_null() const { return type_ == Type::Null; }
    bool is_object() const { return type_ == Type::Object; }
    bool is_array() const { return type_ == Type::Array; }
    bool is_string() const { return type_ == Type::String; }

    // The value, or `fallback` when it has another type.
    bool as_bool(bool fallback = false) const { return type_ == Type::Bool ? bool_ : fallback; }
    double as_number(double fallback = 0) const { return type_ == Type::Number ? number_ : fallback; }
    const std::string& as_string() const;  // "" when not a string

    // Arrays.
    const std::vector<Json>& items() const { return items_; }  // empty when not an array
    void push(Json value);

    // Objects: get() is a null value when the key is missing (or this is not an object);
    // set() replaces the value or adds the key at the end.
    const std::vector<std::pair<std::string, Json>>& members() const { return members_; }
    const Json& get(const std::string& key) const;
    void set(const std::string& key, Json value);

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0;
    std::string string_;
    std::vector<Json> items_;
    std::vector<std::pair<std::string, Json>> members_;
};

// False with a message ("line 3: expected ':'") on a syntax error.
bool json_parse(const std::string& text, Json& out, std::string& error);
// Indented by two spaces, UTF-8 kept as is, a newline at the end.
std::string json_write(const Json& value);

}  // namespace ntr
