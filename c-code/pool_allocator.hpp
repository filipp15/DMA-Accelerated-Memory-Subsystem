#pragma once
// =============================================================================
// PoolAllocator
// -----------------------------------------------------------------------------
// Za razliku od ArenaAllocator-a (koji se prazni samo "sve odjednom"), Pool
// dozvoljava pojedinačno oslobađanje blokova - ali samo ako su svi blokovi
// iste veličine (npr. gomila DMA deskriptora fiksne veličine, ili gomila
// "paketa" podataka iste dužine).
//
// Trik koji ovo čini O(1): kad blok NIJE u upotrebi, njegovih prvih 8 bajtova
// koristimo da čuvamo pokazivač na SLEDEĆI slobodan blok (tzv. "free list",
// odnosno "intrusive linked list"). Ne treba nam posebna struktura sa
// listom slobodnih mesta - sama slobodna memorija JESTE ta lista.
//
// allocate() = skini prvi blok sa vrha liste slobodnih blokova -> O(1)
// deallocate() = vrati blok na vrh liste slobodnih blokova       -> O(1)
// =============================================================================

#include <cstddef>
#include <cstdint>
#include <new>
#include <stdexcept>

namespace mem {

class PoolAllocator {
public:
    // block_size  - veličina jednog "slota" u bajtovima (mora >= sizeof(void*))
    // block_count - koliko slotova pool sadrži ukupno
    // alignment   - poravnanje svakog bloka (podrazumevano 64B, zbog DMA/SIMD)
    PoolAllocator(std::size_t block_size, std::size_t block_count,
                  std::size_t alignment = 64)
        : block_size_(round_up(block_size < sizeof(void*) ? sizeof(void*) : block_size, alignment)),
          block_count_(block_count),
          alignment_(alignment) {
        if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
            throw std::invalid_argument("alignment mora biti stepen dvojke");
        }

        std::size_t total = block_size_ * block_count_;
        buffer_ = static_cast<std::byte*>(::operator new(total, std::align_val_t(alignment_)));

        // Povezujemo sve blokove u free-list, jedan za drugim.
        free_list_ = reinterpret_cast<FreeNode*>(buffer_);
        for (std::size_t i = 0; i < block_count_ - 1; ++i) {
            FreeNode* node = block_at(i);
            node->next = block_at(i + 1);
        }
        block_at(block_count_ - 1)->next = nullptr;

        free_count_ = block_count_;
    }

    ~PoolAllocator() {
        ::operator delete(buffer_, std::align_val_t(alignment_));
    }

    PoolAllocator(const PoolAllocator&) = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;

    // O(1): skida prvi čvor sa vrha free-liste
    [[nodiscard]] void* allocate() noexcept {
        if (free_list_ == nullptr) {
            return nullptr; // pool je pun
        }
        FreeNode* node = free_list_;
        free_list_ = free_list_->next;
        --free_count_;
        return static_cast<void*>(node);
    }

    // O(1): vraća blok nazad na vrh free-liste
    void deallocate(void* ptr) noexcept {
        if (ptr == nullptr) return;
        FreeNode* node = static_cast<FreeNode*>(ptr);
        node->next = free_list_;
        free_list_ = node;
        ++free_count_;
    }

    [[nodiscard]] std::size_t block_size() const noexcept { return block_size_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return block_count_; }
    [[nodiscard]] std::size_t free_count() const noexcept { return free_count_; }
    [[nodiscard]] std::byte* base() const noexcept { return buffer_; }

private:
    struct FreeNode {
        FreeNode* next;
    };

    FreeNode* block_at(std::size_t index) const noexcept {
        return reinterpret_cast<FreeNode*>(buffer_ + index * block_size_);
    }

    static std::size_t round_up(std::size_t n, std::size_t multiple) noexcept {
        return (n + multiple - 1) / multiple * multiple;
    }

    std::byte* buffer_ = nullptr;
    std::size_t block_size_;
    std::size_t block_count_;
    std::size_t alignment_;
    FreeNode* free_list_ = nullptr;
    std::size_t free_count_ = 0;
};

} // namespace mem
