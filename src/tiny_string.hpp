#pragma once
#include <concepts>
#include <memory>
#include <string_view>
#include <utility>

namespace ineffa {
class alignas(char*) tiny_string {
private:
    char sso_[12];
    uint32_t size_;

    auto ptr() const noexcept -> char*& {
        return *std::launder((char**)sso_);
    }

public:
    tiny_string() noexcept : size_(0) {}

    tiny_string(std::string_view sv) : size_(sv.size()) {
        if (size_ > 12)
            std::memcpy(ptr() = new char[size_], sv.data(), size_);
        else if (size_ > 0)
            std::memcpy(sso_, sv.data(), size_);
    }

    tiny_string(tiny_string&& other) noexcept : size_(other.size_) {
        if (size_ > 12) {
            ptr() = other.ptr();
            other.size_ = 0;
        }
        else if (size_ > 0)
            std::memcpy(sso_, other.sso_, size_);
    }

    tiny_string(const tiny_string& other) : tiny_string(other.sv()) {}

    auto operator=(tiny_string&& other) noexcept -> tiny_string& {
        if (this != &other) [[likely]] {
            std::destroy_at(this);
            std::construct_at(this, std::move(other));
        }
        return *this;
    }

    auto operator=(const tiny_string& other) -> tiny_string& {
        if (this != &other) [[likely]] {
            tiny_string temp(other);
            std::destroy_at(this);
            std::construct_at(this, std::move(temp));
        }
        return *this;
    };

    ~tiny_string() {
        if (size_ > 12) delete[] ptr();
    }

    auto data() const noexcept -> const char* {
        return size_ > 12 ? ptr() : sso_;
    }

    auto size() const noexcept -> uint32_t {
        return size_;
    }

    auto sv() const noexcept -> std::string_view {
        return std::string_view(data(), size());
    }

    operator std::string_view() const noexcept {
        return std::string_view(data(), size());
    }
};

template<typename T>
requires std::convertible_to<T, std::string_view>
bool operator==(const tiny_string& lhs, const T& rhs) noexcept {
    return lhs.sv() == std::string_view(rhs);
}

template<typename T>
requires std::convertible_to<T, std::string_view>
auto operator<=>(const tiny_string& lhs, const T& rhs) noexcept {
    return lhs.sv() <=> std::string_view(rhs);
}

}