#include <vector>
#include <source_location>
#include <string>
#include <print>

#include "../src/flat_hash_map.hpp"
#include "../src/tiny_string.hpp"


template <typename K = std::string>
requires std::constructible_from<std::string_view, K> && std::convertible_to<K, std::string_view>
void test_flat_hash_map() {
    using MapType = ineffa::flat_hash_map<K, int, ineffa::hash<std::string_view>>;

    auto check = [](bool condition, const char* expr, const std::source_location loc = std::source_location::current()) {
        if (!condition) [[unlikely]] {
            std::println(stderr, "Test Failed in {}:{}", loc.file_name(), loc.line());
            std::println(stderr, "Expr: {}", expr);
            std::abort();
        }
    };

    #define CHECK(...) check(__VA_ARGS__, #__VA_ARGS__)

    // Initialization and basic capacity tests
    {
        MapType map;
        CHECK(map.empty());
        CHECK(map.size() == 0);
        CHECK(map.capacity() == 0);
        CHECK(map.find("Ghost") == map.end());
    }

    // Insertion, query and Heterogeneous Lookup
    {
        MapType map = {{"Alice", 100}, {"Bob", 200}};
        CHECK(map.size() == 2);
        CHECK(map.contains("Alice"));
        CHECK(map["Bob"] == 200);

        auto [it, inserted] = map.try_emplace("Alice", 999);
        CHECK(!inserted);
        CHECK(it->second == 100);

        map["Alice"] = 150;
        CHECK(map["Alice"] == 150);
        map["Charlie"] = 300;
        CHECK(map.size() == 3);
    }

    // Erasure and Tombstone logic verification
    {
        MapType map = {{"Mike", 120}, {"Lily", 1000}, {"John", 50}};
        CHECK(map.erase("Mike") == 1);
        CHECK(!map.contains("Mike"));
        CHECK(map.size() == 2);
        
        CHECK(map.erase("Ghost") == 0);
        CHECK(map.size() == 2);
        
        CHECK(map.contains("Lily"));
        CHECK(map["John"] == 50);
    }

    // Large-scale insertion and Rehash memory aliasing/lifetime stress test
    {
        MapType map;
        constexpr int TEST_SIZE = 10000;
        for (int i = 0; i < TEST_SIZE; ++i)
            map[std::to_string(i)] = i * 10;

        CHECK(map.size() == TEST_SIZE);
        CHECK(map.capacity() >= TEST_SIZE);
        
        CHECK(map["500"] == 5000);
        CHECK(map["9999"] == 99990);
    }

    // Iterator integrity and Range-based iteration
    {
        MapType map = { {"A", 1}, {"B", 2}, {"C", 3} };
        size_t count = 0;
        int sum = 0;
        for (const auto& [k, v] : map) {
            count++;
            sum += v;
        }
        CHECK(count == 3);
        CHECK(sum == 6);
    }

    // Move and Clear
    {
        MapType map1 = {{ "Data", 42 }};
        MapType map2 = std::move(map1);
        
        CHECK(map1.empty());
        CHECK(map1.capacity() == 0);
        CHECK(map1.size() == 0);
        
        CHECK(map2.size() == 1);
        CHECK(map2["Data"] == 42);

        map2.clear();
        CHECK(map2.empty());
        CHECK(map2.size() == 0);
        CHECK(map2.capacity() > 0);
    }
    
    // Destructor test (RAII check)
    {
        ineffa::flat_hash_map<K, std::vector<int>, ineffa::hash<std::string_view>> map;
        map["Vector1"] = { 1, 2, 3, 4, 5 };
        map.erase("Vector1");
        CHECK(map.empty());
    }

}

auto main() -> int try {
    std::println("Starting ineffa::flat_hash_map tests...");
    test_flat_hash_map<ineffa::tiny_string>();
    std::println("All ineffa::flat_hash_map tests passed successfully");
    return 0;
}
catch(std::exception& e) {
    std::println(stderr, "fetal error: {}", e.what());
    return -1;
}