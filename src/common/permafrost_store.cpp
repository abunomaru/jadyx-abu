// SPDX-FileCopyrightText: Copyright 2026 Jadyx Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/permafrost_store.h"

#include <cstring>
#include <vector>

#include <lz4.h>

namespace Common::Permafrost {

CompressedPages::CompressedPages(size_t page_size_, u64 budget_bytes)
    : page_size{page_size_}, budget{budget_bytes} {}

CompressedPages::~CompressedPages() = default;

bool CompressedPages::Put(u32 slot, u32 page, const u8* data) {
    const int limit = static_cast<int>(page_size / 2);
    // Compressed outside the lock: writers run in parallel.
    thread_local std::vector<char> scratch;
    scratch.resize(static_cast<size_t>(LZ4_compressBound(static_cast<int>(page_size))));
    const int size = LZ4_compress_default(reinterpret_cast<const char*>(data), scratch.data(),
                                          static_cast<int>(page_size),
                                          static_cast<int>(scratch.size()));
    if (size <= 0 || size > limit) {
        refused.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    Entry entry{std::make_unique<u8[]>(static_cast<size_t>(size)), static_cast<u32>(size)};
    std::memcpy(entry.data.get(), scratch.data(), static_cast<size_t>(size));
    const u64 key = Key(slot, page);
    std::scoped_lock lock{mutex};
    // An older copy of the same page is replaced, so its room counts as free.
    const auto old = entries.find(key);
    const u64 freed = old != entries.end() ? Cost(old->second.size) : 0;
    if (bytes.load(std::memory_order_relaxed) - freed + Cost(entry.size) > budget) {
        if (old != entries.end()) EraseLocked(key);
        refused.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    if (old != entries.end()) EraseLocked(key);
    bytes.fetch_add(Cost(entry.size), std::memory_order_relaxed);
    pages.fetch_add(1, std::memory_order_relaxed);
    entries.emplace(key, std::move(entry));
    return true;
}

bool CompressedPages::Get(u32 slot, u32 page, u8* out) const {
    std::scoped_lock lock{mutex};
    const auto it = entries.find(Key(slot, page));
    if (it == entries.end()) {
        return false;
    }
    const int size = LZ4_decompress_safe(reinterpret_cast<const char*>(it->second.data.get()),
                                         reinterpret_cast<char*>(out),
                                         static_cast<int>(it->second.size),
                                         static_cast<int>(page_size));
    if (size != static_cast<int>(page_size)) {
        return false;
    }
    hits.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool CompressedPages::Contains(u32 slot, u32 page) const {
    std::scoped_lock lock{mutex};
    return entries.contains(Key(slot, page));
}

void CompressedPages::Erase(u32 slot, u32 page) {
    std::scoped_lock lock{mutex};
    EraseLocked(Key(slot, page));
}

void CompressedPages::EraseSlot(u32 slot, u32 pages_per_slot) {
    std::scoped_lock lock{mutex};
    for (u32 page = 0; page < pages_per_slot; ++page) {
        EraseLocked(Key(slot, page));
    }
}

void CompressedPages::EraseLocked(u64 key) {
    const auto it = entries.find(key);
    if (it == entries.end()) {
        return;
    }
    bytes.fetch_sub(Cost(it->second.size), std::memory_order_relaxed);
    pages.fetch_sub(1, std::memory_order_relaxed);
    entries.erase(it);
}

} // namespace Common::Permafrost
