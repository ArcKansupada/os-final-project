//
// Created by mtr2462 on 10/13/26.
//

#pragma once
#include <cstdint>
#include <optional>

namespace impl
{
template<typename T> class ChainingHashMap
{
struct Chain
{
    Chain *next;
    uint64_t key;
    T item;
};

uint64_t buckets; // power of 2
uint64_t items;

Chain **table;

[[nodiscard]] uint64_t hash(uint64_t key) const
{
    return ((key ^ (key>>16)) * 11400714819323198485ULL) & (buckets-1);
}

void rehash()
{
    // inefficient, but should be fine for now
    uint64_t old_buckets = buckets;
    buckets *= 2;

    auto *old_table = table;
    table = new Chain*[buckets]();

    for (uint64_t i = 0; i < old_buckets; i++)
    {
        auto *node = old_table[i];
        while (node)
        {
            auto *next = node->next;

            Chain **chain = table + hash(node->key);
            node->next = nullptr;
            while (*chain) chain = &(*chain)->next;
            *chain = node;

            node = next;
        }
    }
    delete[] old_table;
}

public:
    explicit ChainingHashMap(uint64_t default_size)
    {
        buckets = default_size;
        items = 0;
        table = new Chain*[buckets]();
    }

    void remove(uint64_t key)
    {
        Chain **chain = table + hash(key);
        while (*chain)
        {
            if ((*chain)->key == key)
            {
                auto *ptr = *chain;
                *chain = (*chain)->next;
                delete ptr;

                items--;
                return;
            }
            chain = &(*chain)->next;
        }
    }

    void put(uint64_t key, T item)
    {
        Chain **chain = table + hash(key);
        while (*chain)
        {
            if ((*chain)->key == key)
            {
                (*chain)->item = item;
                return;
            }
            chain = &(*chain)->next;
        }
        *chain = new Chain{nullptr, key, item};

        items++;

        if (items > buckets*0.75f) rehash();
    }

    std::optional<T> get(uint64_t key) const
    {
        Chain *chain = table[hash(key)];
        while (chain)
        {
            if (chain->key == key) return chain->item;
            chain = chain->next;
        }
        return std::nullopt;
    }
};
}
