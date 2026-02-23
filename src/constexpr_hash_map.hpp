#pragma once
#include <optional>
#include <array>
#include <stdexcept>
#include <string_view>
#include "./hash.hpp"

namespace ineffa {
template <typename Key, typename Value, size_t N, typename Hash = ineffa::hash<Key>>
requires hashable<Hash, Key>
class constexpr_hash_map {
public:
    static constexpr uint32_t capacity = N * 8 / 7;

private:
    struct ctrl_slot_t {
        static constexpr uint32_t EMPTY_DIB = std::numeric_limits<uint32_t>::max();

        uint32_t hash = 0;
        uint32_t dib = EMPTY_DIB;  // Distance from Initial Bucket

        constexpr bool is_empty() const noexcept {
            return dib == EMPTY_DIB;
        }
    };

    struct kv_slot_t {
        std::pair<Key, Value> kv;

        constexpr auto key() const noexcept -> const Key& { return kv.first; }
        constexpr auto value() const noexcept -> const Value& { return kv.second; }
    };

    std::array<ctrl_slot_t, capacity> ctrl_slots_ = {};
    std::array<kv_slot_t, capacity> kv_slots_ = {};

    #if defined(_MSC_VER)
        [[msvc::no_unique_address]] Hash hash_func_ = {};
    #else
        [[no_unique_address]] Hash hash_func_ = {};
    #endif

public:
    constexpr constexpr_hash_map(const std::pair<Key, Value>(&init_list)[N]) {
        for (const auto& [key, value] : init_list) {
            ctrl_slot_t ctrl_slot = { .hash = (uint32_t)hash_func_(key), .dib = 0 };
            kv_slot_t kv_slot = {{ key, value }};
            uint32_t idx = uint32_t(((uint64_t)ctrl_slot.hash * (uint64_t)capacity) >> 32);

            while (true) {
                if (ctrl_slots_[idx].is_empty()) {
                    ctrl_slots_[idx] = ctrl_slot;
                    kv_slots_[idx] = std::move(kv_slot);
                    break;
                }

                if (ctrl_slots_[idx].hash == ctrl_slot.hash && kv_slots_[idx].key() == kv_slot.key()) [[unlikely]]
                    throw std::logic_error("duplicate keys are not allowed");

                if (ctrl_slots_[idx].dib < ctrl_slot.dib) {
                    std::swap(ctrl_slots_[idx], ctrl_slot);
                    std::swap(kv_slots_[idx], kv_slot);
                }

                idx = idx + 1 == capacity ? 0 : idx + 1;
                ctrl_slot.dib++;
            }
        }
    }

    constexpr auto operator()(const Key& key) const noexcept -> std::optional<const Value> {
        const uint32_t hash = hash_func_(key);
        uint32_t dib = 0;
        uint32_t idx = uint32_t(((uint64_t)hash * (uint64_t)capacity) >> 32);

        while (!ctrl_slots_[idx].is_empty()) {
            if (ctrl_slots_[idx].hash == hash && kv_slots_[idx].key() == key)
                return kv_slots_[idx].value();

            if (ctrl_slots_[idx].dib < dib)
                break;

            idx = idx + 1 == capacity ? 0 : idx + 1;
            dib++;
        }

        return std::nullopt;
    }

    constexpr auto operator[](const Key& key) const -> Value {
        if (auto opt = (*this)(key); opt.has_value()) [[likely]]
            return opt.value();
        throw std::out_of_range("key not found in this map");
    }
};

template <typename Key, typename Value, size_t N, typename Hash = ineffa::hash<Key>>
constexpr auto make_constexpr_hash_map(const std::pair<Key, Value>(&init_list)[N]) {
    return constexpr_hash_map<Key, Value, N, Hash>(init_list);
}

}; // namespace ineffa
