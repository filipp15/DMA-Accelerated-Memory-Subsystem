#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <stdexcept>

namespace mem {

class ArenaAllocator {
public:
    explicit ArenaAllocator(std::size_t size_bytes,
                             std::size_t default_alignment = 64)
        : size_(size_bytes), alignment_(default_alignment) {
        if (default_alignment == 0 || (default_alignment & (default_alignment - 1)) != 0) {
            throw std::invalid_argument("alignment mora biti stepen dvojke (npr. 16, 32, 64)");
        }
        std::size_t rounded = round_up(size_bytes, alignment_);
        buffer_ = static_cast<std::byte*>(::operator new(rounded, std::align_val_t(alignment_)));
        size_ = rounded;
        offset_ = 0;
    }

    ~ArenaAllocator() {
        ::operator delete(buffer_, std::align_val_t(alignment_));
    }

    ArenaAllocator(const ArenaAllocator&) = delete;
    ArenaAllocator& operator=(const ArenaAllocator&) = delete;

    [[nodiscard]] void* allocate(std::size_t n_bytes, std::size_t alignment = 0) noexcept {
        if (alignment == 0) alignment = alignment_;

        std::byte* current = buffer_ + offset_;
        std::size_t space_left = size_ - offset_;

        void* aligned_ptr = current;
        void* result = std::align(alignment, n_bytes, aligned_ptr, space_left);
        if (result == nullptr) {
            return nullptr;
        }

        std::byte* new_cursor = static_cast<std::byte*>(aligned_ptr) + n_bytes;
        offset_ = static_cast<std::size_t>(new_cursor - buffer_);
        return aligned_ptr;
    }

    template <typename T>
    [[nodiscard]] T* allocate(std::size_t count = 1) noexcept {
        void* raw = allocate(sizeof(T) * count, alignof(T) > alignment_ ? alignof(T) : alignment_);
        return static_cast<T*>(raw);
    }

    void reset() noexcept { offset_ = 0; }

    [[nodiscard]] std::size_t capacity() const noexcept { return size_; }
    [[nodiscard]] std::size_t used() const noexcept { return offset_; }
    [[nodiscard]] std::size_t remaining() const noexcept { return size_ - offset_; }

    [[nodiscard]] std::byte* base() const noexcept { return buffer_; }

private:
    static std::size_t round_up(std::size_t n, std::size_t multiple) noexcept {
        return (n + multiple - 1) / multiple * multiple;
    }

    std::byte* buffer_ = nullptr;
    std::size_t size_;
    std::size_t offset_ = 0;
    std::size_t alignment_;
};

} 
