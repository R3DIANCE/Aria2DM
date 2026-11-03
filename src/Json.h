#pragma once

#include <string>
#include <vector>
#include <utility>
#include <cstdint>
#include <cstddef>

// Small dependency-free JSON value used for the aria2 JSON-RPC protocol.
class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool v) : type_(Type::Bool), bool_(v) {}
    Json(int v) : type_(Type::Number), number_((double)v) {}
    Json(long long v) : type_(Type::Number), number_((double)v) {}
    Json(double v) : type_(Type::Number), number_(v) {}
    Json(const char* v) : type_(Type::String), string_(v ? v : "") {}
    Json(std::string v) : type_(Type::String), string_(std::move(v)) {}

    static Json Array() {
        Json j;
        j.type_ = Type::Array;
        return j;
    }
    static Json Object() {
        Json j;
        j.type_ = Type::Object;
        return j;
    }

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    bool asBool(bool fallback = false) const {
        if (type_ == Type::Bool) return bool_;
        if (type_ == Type::Number) return number_ != 0.0;
        return fallback;
    }

    double asDouble(double fallback = 0.0) const {
        if (type_ == Type::Number) return number_;
        if (type_ == Type::String) {
            try {
                return std::stod(string_);
            } catch (...) {
            }
        }
        return fallback;
    }

    long long asInt(long long fallback = 0) const {
        if (type_ == Type::Number) return (long long)number_;
        if (type_ == Type::String) {
            try {
                return std::stoll(string_);
            } catch (...) {
            }
        }
        return fallback;
    }

    const std::string& asString() const { return string_; }

    size_t size() const {
        if (type_ == Type::Array) return array_.size();
        if (type_ == Type::Object) return object_.size();
        return 0;
    }

    const Json& operator[](size_t index) const {
        static const Json null;
        if (type_ != Type::Array || index >= array_.size()) return null;
        return array_[index];
    }

    Json& operator[](size_t index) {
        if (type_ != Type::Array) {
            type_ = Type::Array;
            array_.clear();
        }
        if (index >= array_.size()) array_.resize(index + 1);
        return array_[index];
    }

    const Json& operator[](const std::string& key) const {
        static const Json null;
        if (type_ != Type::Object) return null;
        for (const auto& kv : object_)
            if (kv.first == key) return kv.second;
        return null;
    }

    Json& operator[](const std::string& key) {
        if (type_ != Type::Object) {
            type_ = Type::Object;
            object_.clear();
        }
        for (auto& kv : object_)
            if (kv.first == key) return kv.second;
        object_.emplace_back(key, Json());
        return object_.back().second;
    }

    bool has(const std::string& key) const {
        if (type_ != Type::Object) return false;
        for (const auto& kv : object_)
            if (kv.first == key) return true;
        return false;
    }

    void push(Json value) {
        if (type_ != Type::Array) {
            type_ = Type::Array;
            array_.clear();
        }
        array_.push_back(std::move(value));
    }

    void set(std::string key, Json value) { (*this)[key] = std::move(value); }

    const std::vector<Json>& items() const { return array_; }

    std::string dump() const;
    static Json Parse(const std::string& text, std::string* error = nullptr);

private:
    void DumpInto(std::string& out) const;

    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
    std::vector<Json> array_;
    std::vector<std::pair<std::string, Json>> object_;
};
