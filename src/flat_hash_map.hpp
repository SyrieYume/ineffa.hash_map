#pragma once
#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>
#include "./hash.hpp"

namespace ineffa {

template <typename Key, typename Value, typename Hash = ineffa::hash<Key>, typename KeyEqual = std::equal_to<>>
requires hashable<Hash, Key>
class flat_hash_map {
private:
    template <bool is_const>
    class iterator_impl_t;

public:
    using key_type        = Key;
    using mapped_type     = Value;
    using value_type      = std::pair<const key_type, mapped_type>;
    using size_type       = uint32_t;
    using difference_type = std::ptrdiff_t;
    using hasher          = Hash;
    using key_equal       = KeyEqual;
    using reference       = value_type&;
    using const_reference = const value_type&;
    using pointer         = value_type*;
    using const_pointer   = const value_type*;
    using iterator        = iterator_impl_t<false>;
    using const_iterator  = iterator_impl_t<true>;

private:
    struct ctrl_slot_t {
        static constexpr size_type EMPTY_DIB = std::numeric_limits<size_type>::max();

        size_type hash = 0;
        size_type dib = EMPTY_DIB;  // Distance from Initial Bucket

        constexpr bool is_empty() const noexcept {
            return dib == EMPTY_DIB;
        }
    };

    struct kv_slot_t {
        using kv_type = std::pair<std::remove_const_t<Key>, std::remove_const_t<Value>>;

        constexpr auto kv_ptr() const noexcept -> kv_type* { return (kv_type*)data_; }
        constexpr auto kv() const noexcept -> kv_type& { return *std::launder(kv_ptr()); }
        constexpr auto key() const noexcept -> const Key& { return kv_ptr()->first; }
        constexpr auto value() noexcept -> Value& { return kv_ptr()->second; }

        private:
            alignas(kv_type) std::byte data_[sizeof(kv_type)];
    };

    static_assert(std::is_move_constructible_v<typename kv_slot_t::kv_type>);
    static_assert(std::is_move_assignable_v<typename kv_slot_t::kv_type>);
    static_assert(std::is_nothrow_move_constructible_v<typename kv_slot_t::kv_type>);
    static_assert(std::is_nothrow_move_assignable_v<typename kv_slot_t::kv_type>);
    static_assert(std::is_nothrow_destructible_v<typename kv_slot_t::kv_type>);

    struct aligned_deleter {
        void operator()(std::byte* ptr) const noexcept {
            ::operator delete[](ptr, std::align_val_t { std::max(alignof(ctrl_slot_t), alignof(kv_slot_t)) });
        }
    };

    std::unique_ptr<std::byte[], aligned_deleter> data_ = nullptr;
    size_type size_ = 0;
    size_type capacity_ = 0;

    #if defined(_MSC_VER)
        [[msvc::no_unique_address]] Hash hash_func_ = {};
        [[msvc::no_unique_address]] KeyEqual is_key_equal_ = {};
    #else
        [[no_unique_address]] Hash hash_func_ = {};
        [[no_unique_address]] KeyEqual is_key_equal_ = {};
    #endif

    inline void destroy_kv(ctrl_slot_t& ctrl_slot, kv_slot_t& kv_slot) noexcept {
        std::destroy_at(kv_slot.kv_ptr());
        ctrl_slot.dib = ctrl_slot_t::EMPTY_DIB;
    }

    void insert_for_rehash(const size_type hash, kv_slot_t::kv_type&& kv) noexcept {
        ctrl_slot_t* __restrict ctrl_slots = get_ctrl_slots();
        ctrl_slot_t ctrl_slot = { .hash = hash, .dib = 0 };
        size_type idx = ((uint64_t)hash * (uint64_t)capacity_) >> 32;

        while (true) {
            if (ctrl_slots[idx].is_empty()) {
                ctrl_slots[idx] = ctrl_slot;
                std::construct_at(get_kv_slots()[idx].kv_ptr(), std::move(kv));
                return;
            }

            if (ctrl_slots[idx].dib < ctrl_slot.dib) {
                std::swap(ctrl_slots[idx], ctrl_slot);
                std::swap(get_kv_slots()[idx].kv(), kv);
            }

            idx = idx + 1 == capacity_ ? 0 : idx + 1;
            ctrl_slot.dib++;
        }
    }

    void rehash(size_type new_capacity) {
        constexpr size_type alignment = std::max(alignof(ctrl_slot_t), alignof(kv_slot_t));
        const size_type new_data_size = ((sizeof(ctrl_slot_t) * new_capacity + alignof(kv_slot_t) - 1) & ~(alignof(kv_slot_t) - 1)) + sizeof(kv_slot_t) * new_capacity;
        std::byte* new_data_mem = (std::byte*)::operator new[](new_data_size, std::align_val_t(alignment));
        auto new_data = std::unique_ptr<std::byte[], aligned_deleter>(new_data_mem);

        const ctrl_slot_t* __restrict const old_ctrl_slots = get_ctrl_slots();
        kv_slot_t* __restrict const old_kv_slots = get_kv_slots();
        const auto old_data = std::move(data_);
        const size_type old_capacity = capacity_;

        data_ = std::move(new_data);
        capacity_ = new_capacity;
        ctrl_slot_t* __restrict ctrl_slots = reinterpret_cast<ctrl_slot_t*>(data_.get());
        kv_slot_t* __restrict kv_slots = reinterpret_cast<kv_slot_t*>(data_.get() + ((sizeof(ctrl_slot_t) * capacity_ + alignof(kv_slot_t) - 1) & ~(alignof(kv_slot_t) - 1)));
        std::uninitialized_default_construct_n(ctrl_slots, new_capacity);

        for (size_type idx = 0; idx < old_capacity; idx++)
            if (!old_ctrl_slots[idx].is_empty()) [[likely]] {
                insert_for_rehash(old_ctrl_slots[idx].hash, std::move(old_kv_slots[idx].kv()));
                std::destroy_at(old_kv_slots[idx].kv_ptr());
            }
    }

    auto get_ctrl_slots() const noexcept -> ctrl_slot_t* {
        return std::launder((ctrl_slot_t*)std::assume_aligned<alignof(ctrl_slot_t)>(data_.get()));
    }
    
    auto get_kv_slots() const noexcept -> kv_slot_t* {
        return std::launder((kv_slot_t*)std::assume_aligned<alignof(kv_slot_t)>(
            data_.get() + ((sizeof(ctrl_slot_t) * capacity_ + alignof(kv_slot_t) - 1) & ~(alignof(kv_slot_t) - 1))
        ));
    }

    template <typename H, typename K>
    struct key_type_trait {
        static constexpr bool is_small_trivial = std::is_trivially_copyable_v<K> && (sizeof(K) <= sizeof(size_t) * 2);
        using insert_type = K;
        using query_type = std::conditional_t<is_small_trivial, const K, const K&>;
    };

    template <typename H, typename K>
    requires requires { typename H::transparent_type; }
    struct key_type_trait<H, K> {
        using query_type = typename H::transparent_type;
        using insert_type = typename H::transparent_type;
    };

public:
    flat_hash_map() noexcept = default;

    flat_hash_map(const std::initializer_list<std::pair<typename key_type_trait<Hash, Key>::insert_type, mapped_type>> init_list) {
        size_type required_capacity = 8;
        for (; required_capacity * 7 / 8 < init_list.size(); required_capacity = required_capacity * 3 / 2);
        rehash(required_capacity);
        for (const auto& kv : init_list)
            insert(kv);
    }

    auto erase(key_type_trait<Hash, Key>::query_type key) -> size_type {
        if (capacity_ == 0) [[unlikely]]
            return 0;
        
        ctrl_slot_t* __restrict ctrl_slots = get_ctrl_slots();
        const size_type hash = hash_func_(key);
        size_type idx = ((uint64_t)(uint32_t)hash * (uint64_t)capacity_) >> 32;
        size_type dib = 0;

        while (!ctrl_slots[idx].is_empty()) {
            if (ctrl_slots[idx].hash == hash) [[unlikely]] {
                if (kv_slot_t* kv_slots = get_kv_slots(); is_key_equal_(kv_slots[idx].key(), key)) [[likely]] {
                    size_--;
                    destroy_kv(ctrl_slots[idx], kv_slots[idx]);

                    while (true) {
                        size_type next_idx = idx + 1 == capacity_ ? 0 : idx + 1;;

                        if (ctrl_slots[next_idx].is_empty() || ctrl_slots[next_idx].dib == 0) [[unlikely]]
                            break;

                        ctrl_slots[idx] = ctrl_slots[next_idx];
                        std::construct_at(kv_slots[idx].kv_ptr(), std::move(kv_slots[next_idx].kv()));
                        destroy_kv(ctrl_slots[next_idx], kv_slots[next_idx]);

                        ctrl_slots[idx].dib--;
                        idx = next_idx;
                    }
                    
                    return 1;
                }
            }

            if (ctrl_slots[idx].dib < dib) [[unlikely]]
                return 0;

            idx = idx + 1 == capacity_ ? 0 : idx + 1;
            dib++;
        }

        return 0;
    }

    template <typename Self>
    auto find(this Self&& self, key_type_trait<Hash, Key>::query_type key) -> std::conditional_t<std::is_const_v<std::remove_reference_t<Self>>, const_iterator, iterator> {
        if (self.capacity_ == 0) [[unlikely]]
            return self.end();

        const ctrl_slot_t* __restrict ctrl_slots = self.get_ctrl_slots();
        const size_type hash = self.hash_func_(key);
        size_type idx = ((uint64_t)(uint32_t)hash * (uint64_t)self.capacity_) >> 32;
        size_type dib = 0;

        while (!ctrl_slots[idx].is_empty()) {
            if (ctrl_slots[idx].hash == hash) [[unlikely]]
                if (self.is_key_equal_(self.get_kv_slots()[idx].key(), key)) [[likely]]
                    return { &self, idx };

            if (ctrl_slots[idx].dib < dib) [[unlikely]]
                break;
            
            idx = idx + 1 == self.capacity_ ? 0 : idx + 1;
            dib++;
        }

        return self.end();
    }

    template <typename... Args>
    auto try_emplace(key_type_trait<Hash, Key>::query_type key, Args&&... args) -> std::pair<iterator, bool> {
        if (size_ >= capacity_ * 7 / 8) [[unlikely]]
            rehash(capacity_ == 0 ? 8 : capacity_ * 3 / 2);

        ctrl_slot_t* __restrict ctrl_slots = get_ctrl_slots();
        ctrl_slot_t ctrl_slot = { .hash = (size_type)hash_func_(key), .dib = 0 };
        size_type idx = ((uint64_t)(uint32_t)ctrl_slot.hash * (uint64_t)capacity_) >> 32;

        while (true) {
            if (ctrl_slots[idx].is_empty()) {
                ctrl_slots[idx] = ctrl_slot;
                std::construct_at(get_kv_slots()[idx].kv_ptr(), std::piecewise_construct, std::forward_as_tuple(key), std::forward_as_tuple(args...));
                size_++;
                return { iterator(this, idx), true };
            }

            if (ctrl_slots[idx].hash == ctrl_slot.hash) [[unlikely]]
                if (kv_slot_t* kv_slots = get_kv_slots(); is_key_equal_(kv_slots[idx].key(), key)) [[likely]]
                    return { iterator(this, idx), false };
            
            if (ctrl_slots[idx].dib < ctrl_slot.dib) [[unlikely]] {
                typename kv_slot_t::kv_type new_kv(std::piecewise_construct, std::forward_as_tuple(key), std::forward_as_tuple(args...));
                std::swap(ctrl_slots[idx], ctrl_slot);
                auto& old_kv = get_kv_slots()[idx].kv();
                auto kv = std::move(old_kv);
                old_kv = std::move(new_kv);
                size_type inserted_idx = idx;
                
                while (true) {
                    idx = idx + 1 == capacity_ ? 0 : idx + 1;
                    ctrl_slot.dib++;

                    if (ctrl_slots[idx].is_empty()) {
                        ctrl_slots[idx] = ctrl_slot;
                        std::construct_at(get_kv_slots()[idx].kv_ptr(), std::move(kv));
                        size_++;
                        return { iterator(this, inserted_idx), true };
                    }

                    if (ctrl_slots[idx].dib < ctrl_slot.dib) [[unlikely]] {
                        std::swap(ctrl_slots[idx], ctrl_slot);
                        std::swap(get_kv_slots()[idx].kv(), kv);
                    }
                }

                std::unreachable();
            }

            idx = idx + 1 == capacity_ ? 0 : idx + 1;
            ctrl_slot.dib++;
        }

        std::unreachable();
    }

    auto insert(std::pair<typename key_type_trait<Hash, Key>::query_type, mapped_type> key_and_value) -> std::pair<iterator, bool> {
        return try_emplace(key_and_value.first, std::move(key_and_value.second));
    }

    auto operator[](key_type_trait<Hash, Key>::query_type key) -> mapped_type& {
        return try_emplace(key).first->second;
    }

    auto begin() noexcept -> iterator { return iterator(this, 0); }
    auto end()   noexcept -> iterator { return iterator(this, capacity_); }
    
    auto begin() const noexcept -> const_iterator { return const_iterator(this, 0); }
    auto end()   const noexcept -> const_iterator { return const_iterator(this, capacity_); }

    auto cbegin() const noexcept -> const_iterator { return const_iterator(this, 0); }
    auto cend()   const noexcept -> const_iterator { return const_iterator(this, capacity_); }

    auto size()  const noexcept -> size_type { return size_; }
    auto empty() const noexcept -> bool { return size_ == 0; }
    auto capacity() const noexcept -> size_type { return capacity_; }

    auto contains(key_type_trait<Hash, Key>::query_type key) const noexcept -> bool { return find(key) != end(); }

    void clear() noexcept {
        if (capacity_ == 0) [[unlikely]]
            return;

        ctrl_slot_t* __restrict ctrl_slots_ = get_ctrl_slots();
        kv_slot_t* __restrict kv_slots_ = get_kv_slots();

        for (size_type idx = 0; idx < capacity_; idx++)
            if (!ctrl_slots_[idx].is_empty())
                destroy_kv(ctrl_slots_[idx], kv_slots_[idx]);
        size_ = 0;
    }


    ~flat_hash_map() noexcept {
        clear();
    }

    flat_hash_map(flat_hash_map&& other) noexcept :
        data_(std::move(other.data_)),
        capacity_(std::exchange(other.capacity_, 0)),
        size_(std::exchange(other.size_, 0)),
        hash_func_(std::move(other.hash_func_)),
        is_key_equal_(std::move(other.is_key_equal_))
    {}

    auto operator=(flat_hash_map&& other) noexcept -> flat_hash_map& {
        if (this != &other) [[likely]] {
            clear();
            data_ = std::move(other.data_);
            capacity_ = std::exchange(other.capacity_, 0);
            size_ = std::exchange(other.size_, 0);
            hash_func_ = std::move(other.hash_func_);
            is_key_equal_ = std::move(other.is_key_equal_);
        }
        return *this;
    };

    flat_hash_map(const flat_hash_map&) = delete;
    flat_hash_map& operator=(const flat_hash_map&) = delete;
};


template <typename Key, typename Value, typename Hash, typename KeyEqual>
requires hashable<Hash, Key>
template <bool is_const>
class flat_hash_map<Key, Value, Hash, KeyEqual>::iterator_impl_t {
private:
    using map_type = std::conditional_t<is_const, const flat_hash_map, flat_hash_map>;
    map_type* map_ = nullptr;
    map_type::size_type idx_ = 0;

    void skip_empty() noexcept {
        ctrl_slot_t* __restrict ctrl_slots = map_->get_ctrl_slots();
        auto capacity = map_->capacity_;
        for (; idx_ < capacity && ctrl_slots[idx_].is_empty(); idx_++);
    }

public:
    using iterator_category = std::forward_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = kv_slot_t::kv_type;
    using reference = std::conditional_t<is_const, const value_type&, value_type&>;
    using pointer = std::conditional_t<is_const, const value_type*, value_type*>;

    iterator_impl_t() noexcept = default;

    iterator_impl_t(map_type* map, uint32_t idx) noexcept : map_(map), idx_(idx) {
        if (idx_ < map_->capacity_) [[likely]]
            skip_empty();
    }

    iterator_impl_t(const iterator_impl_t<false>& other) noexcept : map_(other.map_), idx_(other.idx_) {}

    auto operator*() const noexcept -> reference {
        return map_->get_kv_slots()[idx_].kv();
    }

    auto operator->() const noexcept -> pointer {
        return std::addressof(**this);
    }

    auto operator++() noexcept -> iterator_impl_t& {
        idx_++;
        skip_empty();
        return *this;
    }

    auto operator++(int) noexcept -> iterator_impl_t {
        iterator_impl_t tmp = *this;
        ++(*this);
        return tmp;
    }
        
    template <bool C>
    bool operator==(const iterator_impl_t<C>& other) const noexcept { return idx_ == other.idx_; }

    template <bool C>
    bool operator!=(const iterator_impl_t<C>& other) const noexcept { return idx_ != other.idx_; }
};

} // namespace ineffa

