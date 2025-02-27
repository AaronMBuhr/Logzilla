#pragma once
#ifdef INFRASTRUCTURE_STATIC
#define INFRA_API
#else
#ifdef INFRASTRUCTURE_EXPORTS
#define INFRA_API __declspec(dllexport)
#else
#define INFRA_API __declspec(dllimport)
#endif
#endif

#include <mutex>
#include <string>
#include <array>
#include <stdexcept>
#include <cstdio>
#include <algorithm>
#include <cstddef>
#include <atomic>

namespace detail {
    // Removed INFRA_API from BitmapMutex so that std::mutex isn’t exported.
    class BitmapMutex {
    private:
        std::mutex mutex_;
    public:
        BitmapMutex();
        BitmapMutex(const BitmapMutex&) = delete;
        BitmapMutex& operator=(const BitmapMutex&) = delete;

        void lock() noexcept;
        void unlock() noexcept;
        bool try_lock() noexcept;
    };
}

class INFRA_API Bitmap
{
public:
    static constexpr size_t MAX_BITS = 10240; // Adjust this based on your needs
    static constexpr size_t BITS_PER_WORD = sizeof(size_t) * 8;
    static constexpr size_t MAX_WORDS = (MAX_BITS + BITS_PER_WORD - 1) / BITS_PER_WORD;
    static constexpr size_t INVALID_BIT_NUMBER = ~static_cast<size_t>(0);

    // Constructor now takes a size_t for the number of bits, plus an initial bit value (0 or 1).
    Bitmap(size_t number_of_bits, unsigned char initial_bit_value);

    bool isSet(size_t bit_number) const;
    unsigned char bitValue(size_t bit_number) const;

    // Setting bits will update the running tally of set bits.
    void setBitTo(size_t bit_number, unsigned char new_bit_value);

    // Test-and-set / test-and-clear also update the running tally.
    unsigned char testAndSet(size_t bit_number);
    unsigned char testAndClear(size_t bit_number);

    int getFirstZero() const;       // returns -1 if none
    int getAndSetFirstZero();       // returns -1 if none
    int getFirstOne() const;        // returns -1 if none
    int getAndClearFirstOne();      // returns -1 if none

    // These now run in O(1) by returning the cached count of ones (and total minus that).
    int countZeroes() const;
    int countOnes() const;

    std::string asHexString() const;
    std::string asBinaryString() const;

private:
    // Private helper methods that do partial or optional clearing/setting.
    int getAndOptionallyClearFirstOne(bool do_clear) const;
    int getAndOptionallySetFirstZero(bool do_set);

    size_t number_of_bits_;
    size_t number_of_words_;
    std::array<size_t, MAX_WORDS> bitmap_;
    mutable detail::BitmapMutex in_use_;  // used for thread safety in certain methods

    // Running tally of how many bits are set (1).
    // We store it as int, but if you need more bits set than int can hold, use std::atomic<size_t>.
    std::atomic<int> count_of_ones_{ 0 };
};
