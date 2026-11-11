#pragma once

#include <cstddef>
#include <cstdint>

namespace gxos {
namespace javascript {

enum class FrontendStorageError : std::uint8_t {
    None = 0,
    InvalidStorage,
    InvalidAlignment,
    SizeOverflow,
    Exhausted,
    InvalidMark,
};

struct FrontendArenaMark {
    std::size_t offset = 0;
    std::uint64_t generation = 0;
};

// A bounded, caller-owned bump arena. It never grows or frees individual
// allocations. The caller retains ownership of the supplied byte range.
class FrontendArena {
public:
    FrontendArena() = default;
    FrontendArena(void* storage, std::size_t capacity)
    {
        configure(storage, capacity);
    }

    FrontendStorageError configure(void* storage, std::size_t capacity)
    {
        storage_ = nullptr;
        capacity_ = 0;
        used_ = 0;
        highWater_ = 0;
        valid_ = false;
        advanceGeneration();

        if (storage == nullptr && capacity != 0)
            return FrontendStorageError::InvalidStorage;

        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(storage);
        const std::uintptr_t addressMax =
            static_cast<std::uintptr_t>(-1);
        if (capacity > static_cast<std::size_t>(addressMax - base))
            return FrontendStorageError::SizeOverflow;

        storage_ = static_cast<unsigned char*>(storage);
        capacity_ = capacity;
        valid_ = true;
        return FrontendStorageError::None;
    }

    void reset()
    {
        used_ = 0;
        highWater_ = 0;
        advanceGeneration();
    }

    bool allocate(std::size_t size, std::size_t alignment, void*& result,
        FrontendStorageError& error)
    {
        result = nullptr;
        error = FrontendStorageError::None;
        if (!valid_) {
            error = FrontendStorageError::InvalidStorage;
            return false;
        }
        if (alignment == 0 || (alignment & (alignment - 1u)) != 0) {
            error = FrontendStorageError::InvalidAlignment;
            return false;
        }
        if (size == 0) return true;

        const std::uintptr_t addressMax =
            static_cast<std::uintptr_t>(-1);
        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(storage_);
        const std::uintptr_t current = base + used_;
        const std::uintptr_t mask = static_cast<std::uintptr_t>(alignment - 1u);
        if (current > addressMax - mask) {
            error = FrontendStorageError::SizeOverflow;
            return false;
        }
        const std::uintptr_t aligned = (current + mask) & ~mask;
        if (aligned < base) {
            error = FrontendStorageError::SizeOverflow;
            return false;
        }
        const std::uintptr_t offsetAddress = aligned - base;
        if (offsetAddress > static_cast<std::uintptr_t>(capacity_)) {
            error = FrontendStorageError::Exhausted;
            return false;
        }
        const std::size_t offset = static_cast<std::size_t>(offsetAddress);
        const std::size_t sizeMax = static_cast<std::size_t>(-1);
        if (size > sizeMax - offset) {
            error = FrontendStorageError::SizeOverflow;
            return false;
        }
        if (size > capacity_ - offset) {
            error = FrontendStorageError::Exhausted;
            return false;
        }

        result = storage_ + offset;
        used_ = offset + size;
        if (used_ > highWater_) highWater_ = used_;
        return true;
    }

    FrontendArenaMark mark() const
    {
        return FrontendArenaMark{used_, generation_};
    }

    bool restore(FrontendArenaMark checkpoint,
        FrontendStorageError& error)
    {
        if (!valid_ || checkpoint.generation != generation_ ||
            checkpoint.offset > used_) {
            error = FrontendStorageError::InvalidMark;
            return false;
        }
        used_ = checkpoint.offset;
        error = FrontendStorageError::None;
        return true;
    }

    std::size_t capacity() const { return capacity_; }
    std::size_t used() const { return used_; }
    std::size_t highWater() const { return highWater_; }
    bool valid() const { return valid_; }

private:
    void advanceGeneration()
    {
        ++generation_;
        if (generation_ == 0) ++generation_;
    }

    unsigned char* storage_ = nullptr;
    std::size_t capacity_ = 0;
    std::size_t used_ = 0;
    std::size_t highWater_ = 0;
    std::uint64_t generation_ = 0;
    bool valid_ = false;
};

} // namespace javascript
} // namespace gxos
