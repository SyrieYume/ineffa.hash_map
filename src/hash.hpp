#pragma once
#include <concepts>
#include <string_view>

namespace ineffa {
template <typename T>
struct hash;

template <typename T>
requires std::convertible_to<T, std::string_view>
struct hash<T> {
    using is_transparent = void;
    using transparent_type = const std::string_view;

    constexpr static auto operator()(const std::string_view sv) noexcept -> uint64_t {
        uint64_t hash = 14695981039346656037ull;
        for (const char c : sv) {
            hash ^= uint64_t(c);
            hash *= 1099511628211ull;
        }
        return hash;
    }
};

template <typename T>
requires std::convertible_to<T, uint64_t>
struct hash<T> {
    using is_transparent = void;

    constexpr static auto operator()(const T val) noexcept -> uint64_t {
        uint64_t hash = (uint64_t)val + 0x9e3779b97f4a7c15;
        hash = (hash ^ (hash >> 30)) * 0xbf58476d1ce4e5b9;
        hash = (hash ^ (hash >> 27)) * 0x94d049bb133111eb;
        hash = hash ^ (hash >> 31);
        return hash;
    }
};

template <typename Hash, typename Key>
concept hashable = requires(Hash hash_func, const Key& key) {
    { hash_func(key) } -> std::convertible_to<size_t>;
};
}