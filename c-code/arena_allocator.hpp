#pragma once
// =============================================================================
// ArenaAllocator
// -----------------------------------------------------------------------------
// Ideja: unapred rezervišemo jedan veliki, kontinualni blok memorije ("arenu").
// Svaka sledeća alokacija samo pomera "kursor" (offset) unutar te arene i vraća
// pokazivač - nema traženja slobodne rupe kao kod malloc-a, pa je vreme
// alokacije konstantno, O(1), i potpuno predvidivo.
//
// Ograničenje (namerno, po dizajnu): pojedinačni blokovi se ne oslobađaju
// jedan po jedan. Cela arena se oslobađa odjednom pozivom reset(). Ovo je
// idealno za DMA prenose: alociraš scratchpad blok, DMA ga popuni/isprazni,
// i kad je "batch" gotov, resetuješ celu arenu za sledeći.
//
// 64-bajtno poravnanje je bitno jer SIMD instrukcije i DMA kontroleri
// očekuju da podaci počinju na adresama deljivim sa 64 (cache-line/burst
// granica). Ako se to ne poštuje, hardver ili ne može efikasno da čita
// podatke, ili uopšte ne može da ih pročita.
// =============================================================================

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <stdexcept>

namespace mem {

class ArenaAllocator {
public:
    // default_alignment = 64 bajta, zbog SIMD/DMA zahteva pomenutih iznad
    explicit ArenaAllocator(std::size_t size_bytes,
                             std::size_t default_alignment = 64)
        : size_(size_bytes), alignment_(default_alignment) {
        if (default_alignment == 0 || (default_alignment & (default_alignment - 1)) != 0) {
            throw std::invalid_argument("alignment mora biti stepen dvojke (npr. 16, 32, 64)");
        }
        // std::aligned_alloc zahteva da size bude umnožak alignment-a
        std::size_t rounded = round_up(size_bytes, alignment_);
        buffer_ = static_cast<std::byte*>(::operator new(rounded, std::align_val_t(alignment_)));
        size_ = rounded;
        offset_ = 0;
    }

    ~ArenaAllocator() {
        ::operator delete(buffer_, std::align_val_t(alignment_));
    }

    // Zabranjujemo kopiranje - arena poseduje memoriju, ne sme da postoje dva
    // vlasnika istog bafera (dvostruko oslobađanje).
    ArenaAllocator(const ArenaAllocator&) = delete;
    ArenaAllocator& operator=(const ArenaAllocator&) = delete;

    // Alocira `n_bytes`, poravnato na `alignment` (podrazumevano isto kao
    // alignment cele arene, tj. 64B). Vraća nullptr ako nema više mesta -
    // namerno ne bacamo izuzetak ovde jer je ovo "hot path" i pozivalac
    // treba brzo da proveri povratnu vrednost, bez skupog stack unwinding-a.
    [[nodiscard]] void* allocate(std::size_t n_bytes, std::size_t alignment = 0) noexcept {
        if (alignment == 0) alignment = alignment_;

        std::byte* current = buffer_ + offset_;
        std::size_t space_left = size_ - offset_;

        // std::align pronalazi sledeću poravnatu adresu unutar preostalog
        // prostora - i dalje O(1), nema pretrage kroz listu blokova.
        void* aligned_ptr = current;
        void* result = std::align(alignment, n_bytes, aligned_ptr, space_left);
        if (result == nullptr) {
            return nullptr; // arena je puna
        }

        std::byte* new_cursor = static_cast<std::byte*>(aligned_ptr) + n_bytes;
        offset_ = static_cast<std::size_t>(new_cursor - buffer_);
        return aligned_ptr;
    }

    // Tipizovana pogodnost: allocate<T>(count) vraća T* na `count` objekata.
    template <typename T>
    [[nodiscard]] T* allocate(std::size_t count = 1) noexcept {
        void* raw = allocate(sizeof(T) * count, alignof(T) > alignment_ ? alignof(T) : alignment_);
        return static_cast<T*>(raw);
    }

    // "Oslobađa" celu arenu odjednom - samo vraća kursor na 0. Ne poziva
    // destruktore, ovo je namenjeno POD/trivijalnim podacima (npr. sirovi
    // bajtovi koje DMA prenosi), ne C++ objektima sa nekim resursom.
    void reset() noexcept { offset_ = 0; }

    [[nodiscard]] std::size_t capacity() const noexcept { return size_; }
    [[nodiscard]] std::size_t used() const noexcept { return offset_; }
    [[nodiscard]] std::size_t remaining() const noexcept { return size_ - offset_; }

    // Bazna adresa arene - korisno kad hardverskom (simuliranom) DMA
    // kontroleru treba da se prosledi "osnovna adresa" bloka memorije.
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

} // namespace mem
