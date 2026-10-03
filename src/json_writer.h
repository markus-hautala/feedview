// Minimal streaming JSON writer (no dependencies). Usage:
//   JsonWriter j; j.beginObject().key("a").value(1).key("b").beginArray().value("x").endArray().endObject();
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

class JsonWriter {
public:
    JsonWriter& beginObject() { return open('{'); }
    JsonWriter& endObject() { return close('}'); }
    JsonWriter& beginArray() { return open('['); }
    JsonWriter& endArray() { return close(']'); }

    JsonWriter& key(std::string_view k) {
        separator();
        quote(k);
        out_ += ':';
        afterKey_ = true;
        return *this;
    }
    JsonWriter& value(std::string_view s) {
        separator();
        quote(s);
        return *this;
    }
    JsonWriter& value(const char* s) { return s ? value(std::string_view(s)) : null(); }
    JsonWriter& value(const std::string& s) { return value(std::string_view(s)); }
    JsonWriter& value(bool b) {
        separator();
        out_ += b ? "true" : "false";
        return *this;
    }
    JsonWriter& value(int v) { return value(int64_t(v)); }
    JsonWriter& value(unsigned v) { return value(int64_t(v)); }
    JsonWriter& value(int64_t v) {
        separator();
        out_ += std::to_string(v);
        return *this;
    }
    JsonWriter& value(uint64_t v) {
        separator();
        out_ += std::to_string(v);
        return *this;
    }
    JsonWriter& value(double v) {
        separator();
        if (!std::isfinite(v)) {
            out_ += "null";
        } else {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%.6g", v);
            out_ += buf;
        }
        return *this;
    }
    JsonWriter& value(float v) { return value(double(v)); }
    JsonWriter& null() {
        separator();
        out_ += "null";
        return *this;
    }
    // Inserts an already serialised JSON value.
    JsonWriter& raw(std::string_view json) {
        separator();
        out_ += json;
        return *this;
    }
    template <typename T>
    JsonWriter& field(std::string_view k, const T& v) {
        return key(k).value(v);
    }

    const std::string& str() const { return out_; }

    static void escapeTo(std::string& out, std::string_view s) {
        for (unsigned char c : s) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20) {
                        char buf[8];
                        std::snprintf(buf, sizeof buf, "\\u%04x", c);
                        out += buf;
                    } else {
                        out += char(c);  // UTF-8 passes through unchanged
                    }
            }
        }
    }

private:
    JsonWriter& open(char c) {
        separator();
        out_ += c;
        first_.push_back(true);
        return *this;
    }
    JsonWriter& close(char c) {
        out_ += c;
        if (!first_.empty()) first_.pop_back();
        return *this;
    }
    void separator() {
        if (afterKey_) {
            afterKey_ = false;
            return;
        }
        if (!first_.empty()) {
            if (!first_.back()) out_ += ',';
            first_.back() = false;
        }
    }
    void quote(std::string_view s) {
        out_ += '"';
        escapeTo(out_, s);
        out_ += '"';
    }

    std::string out_;
    std::vector<bool> first_;
    bool afterKey_ = false;
};
