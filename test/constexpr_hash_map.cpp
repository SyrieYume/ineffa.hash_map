#include "../src/constexpr_hash_map.hpp"

[[maybe_unused]] static auto constexpr_hash_map_example() -> uint32_t {
    constexpr auto map = ineffa::make_constexpr_hash_map<std::string_view, uint32_t> ({
        { "test1", 1 },
        { "test2", 2 },
        { "test3", 124 },
        { "test4", 223 }
    });

    int n = 12;

    switch(n) {
        case map["test1"] : return 1;
        case map["test2"] : return 2;
        default: break;
    }

    constexpr uint32_t a = map["test3"];
    constexpr uint32_t b = map["test4"];
    return a + b;
}