/* Copyright 2025 Logzilla Corp. */

#pragma once
#ifndef INFRA_API
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

    // Constructor now takes a size_t for the number of bits.
    Bitmap(size_t number_of_bits, unsigned char initial_bit_value);
    bool isSet(size_t bit_number) const;
    unsigned char bitValue(size_t bit_number) const;
    void setBitTo(size_t bit_number, unsigned char new_bit_value);
    unsigned char testAndSet(size_t bit_number);
    unsigned char testAndClear(size_t bit_number);
    int getFirstZero() const; // returns -1 if none
    int getAndSetFirstZero(); // returns -1 if none
    int getFirstOne() const; // returns -1 if none
    int getAndClearFirstOne(); // returns -1 if none
    int countZeroes();
    int countOnes();
    std::string asHexString() const;
    std::string asBinaryString() const;

private:
    size_t number_of_bits_;
    size_t number_of_words_;
    std::array<size_t, MAX_WORDS> bitmap_;
    mutable detail::BitmapMutex in_use_;  // now mutable so it can be locked in const methods

    int getAndOptionallyClearFirstOne(bool do_clear) const;
    int getAndOptionallySetFirstZero(bool do_set);
};
