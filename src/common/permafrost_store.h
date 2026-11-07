// SPDX-FileCopyrightText: Copyright 2026 Jadyx Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "common/common_types.h"

namespace Common::Permafrost {

/// Compressed copies of swapped-out pages kept in memory, ahead of the swap file.
///
/// Permafrost writes idle guest pages to a file and reads them back on a fault: a few milliseconds
/// each, and hundreds when the storage is busy. A page that compresses to half or less is kept here
/// instead, LZ4-compressed, and comes back in microseconds. The store holds at most its budget;
/// what does not fit, or does not compress, goes to the file as before. Pages are keyed by their
/// swap slot (a granule's place in the file) and their page in it, so a page has one copy here or
/// one in the file, never both read back.
class CompressedPages {
public:
    /// page_size is the size of every page put and got.
    CompressedPages(size_t page_size, u64 budget_bytes);
    ~CompressedPages();

    CompressedPages(const CompressedPages&) = delete;
    CompressedPages& operator=(const CompressedPages&) = delete;

    /// Keeps a compressed copy of the page when it compresses to half its size or less and the
    /// budget has room for it; replaces an older copy of the same page. False when not kept: the
    /// page must go to the file.
    bool Put(u32 slot, u32 page, const u8* data);

    /// Decompresses the page into out (page_size bytes). False when it is not here.
    bool Get(u32 slot, u32 page, u8* out) const;

    [[nodiscard]] bool Contains(u32 slot, u32 page) const;

    /// Drops the copy of a page, if any: its data came back or was discarded.
    void Erase(u32 slot, u32 page);

    /// Drops every copy of a slot's pages: the slot was freed.
    void EraseSlot(u32 slot, u32 pages_per_slot);

    /// Memory held, counting each copy's bookkeeping.
    [[nodiscard]] u64 Bytes() const {
        return bytes.load(std::memory_order_relaxed);
    }
    /// Pages held.
    [[nodiscard]] u64 Pages() const {
        return pages.load(std::memory_order_relaxed);
    }
    /// Pages refused for lack of room or because they did not compress well.
    [[nodiscard]] u64 Refused() const {
        return refused.load(std::memory_order_relaxed);
    }
    /// Pages got (read back from memory instead of the file).
    [[nodiscard]] u64 Hits() const {
        return hits.load(std::memory_order_relaxed);
    }

private:
    struct Entry {
        std::unique_ptr<u8[]> data;
        u32 size{};
    };
    static u64 Key(u32 slot, u32 page) {
        return (u64{slot} << 32) | page;
    }
    /// What an entry costs: its compressed bytes and its node.
    static u64 Cost(u32 size) {
        return u64{size} + 64;
    }
    void EraseLocked(u64 key);

    const size_t page_size;
    const u64 budget;
    mutable std::mutex mutex;
    std::unordered_map<u64, Entry> entries;
    std::atomic<u64> bytes{};
    std::atomic<u64> pages{};
    std::atomic<u64> refused{};
    mutable std::atomic<u64> hits{};
};

} // namespace Common::Permafrost
