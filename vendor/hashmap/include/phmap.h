// phmap_aliases.h
#pragma once

#include "phmap/phmap.h"
#include "phmap/btree.h"

// Aliases for hash-based containers (unordered)
template<typename Key, typename Hash = std::hash<Key>, typename Eq = std::equal_to<Key>, typename Alloc = std::allocator<Key>>
using unordered_set = phmap::flat_hash_set<Key, Hash, Eq, Alloc>;

template<typename Key, typename T, typename Hash = std::hash<Key>, typename Eq = std::equal_to<Key>, typename Alloc = std::allocator<std::pair<const Key, T>>>
using unordered_map = phmap::flat_hash_map<Key, T, Hash, Eq, Alloc>;

// Aliases for ordered containers (btree)
template<typename Key, typename Compare = std::less<Key>, typename Alloc = std::allocator<Key>>
using set = phmap::btree_set<Key, Compare, Alloc>;

template<typename Key, typename T, typename Compare = std::less<Key>, typename Alloc = std::allocator<std::pair<const Key, T>>>
using map = phmap::btree_map<Key, T, Compare, Alloc>;

template<typename Key, typename Compare = std::less<Key>, typename Alloc = std::allocator<Key>>
using multiset = phmap::btree_multiset<Key, Compare, Alloc>;

template<typename Key, typename T, typename Compare = std::less<Key>, typename Alloc = std::allocator<std::pair<const Key, T>>>
using multimap = phmap::btree_multimap<Key, T, Compare, Alloc>;

// Optional: aliases for parallel hash tables
template<typename Key, typename Hash = std::hash<Key>, typename Eq = std::equal_to<Key>, typename Alloc = std::allocator<Key>>
using parallel_unordered_set = phmap::parallel_flat_hash_set<Key, Hash, Eq, Alloc>;

template<typename Key, typename T, typename Hash = std::hash<Key>, typename Eq = std::equal_to<Key>, typename Alloc = std::allocator<std::pair<const Key, T>>>
using parallel_unordered_map = phmap::parallel_flat_hash_map<Key, T, Hash, Eq, Alloc>;
