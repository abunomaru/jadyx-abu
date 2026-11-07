// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2022 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include "dynarmic/interface/exclusive_monitor.h"

#include <algorithm>
#include <atomic>

#include "common/assert.h"

namespace Dynarmic {

ExclusiveMonitor::ExclusiveMonitor(std::size_t processor_count_)
        : processor_count{processor_count_} {
    ASSERT(processor_count <= MAX_NUM_CPU_CORES);
}

size_t ExclusiveMonitor::GetProcessorCount() const {
    return processor_count;
}

void ExclusiveMonitor::Lock() {
    lock.Lock();
}

void ExclusiveMonitor::Unlock() {
    lock.Unlock();
}

bool ExclusiveMonitor::CheckAndClear(std::size_t processor_id, VAddr address) {
    const VAddr masked_address = address & RESERVATION_GRANULE_MASK;

#ifdef ARCHITECTURE_arm64
    std::atomic_ref<VAddr> own{exclusive_addresses[processor_id]};
    VAddr expected = masked_address;
    if (own.load(std::memory_order_relaxed) != masked_address ||
        !own.compare_exchange_strong(expected, INVALID_EXCLUSIVE_ADDRESS, std::memory_order_acq_rel)) {
        return false;
    }
    for (VAddr& other_address : exclusive_addresses) {
        std::atomic_ref<VAddr> other{other_address};
        if (other.load(std::memory_order_relaxed) == masked_address) {
            expected = masked_address;
            other.compare_exchange_strong(expected, INVALID_EXCLUSIVE_ADDRESS,
                                          std::memory_order_acq_rel);
        }
    }
    return true;
#endif

    Lock();
    if (reservations[processor_id].address != masked_address) {
        Unlock();
        return false;
    }

    for (size_t i = 0; i < processor_count; ++i) {
        if (reservations[i].address == masked_address) {
            reservations[i].address = INVALID_EXCLUSIVE_ADDRESS;
        }
    }
    return true;
}

void ExclusiveMonitor::Clear() {
#ifdef ARCHITECTURE_arm64
    for (VAddr& address : exclusive_addresses) {
        std::atomic_ref<VAddr>{address}.store(INVALID_EXCLUSIVE_ADDRESS, std::memory_order_release);
    }
#else
    Lock();
    for (size_t i = 0; i < processor_count; ++i) {
        reservations[i].address = INVALID_EXCLUSIVE_ADDRESS;
    }
    Unlock();
#endif
}

void ExclusiveMonitor::ClearProcessor(std::size_t processor_id) {
#ifdef ARCHITECTURE_arm64
    std::atomic_ref<VAddr>{exclusive_addresses[processor_id]}.store(INVALID_EXCLUSIVE_ADDRESS, std::memory_order_release);
#else
    Lock();
    reservations[processor_id].address = INVALID_EXCLUSIVE_ADDRESS;
    Unlock();
#endif
}

}  // namespace Dynarmic
