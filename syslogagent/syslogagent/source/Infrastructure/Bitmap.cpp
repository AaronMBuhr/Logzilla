/* Copyright 2025 Logzilla Corp. */

#include "pch.h"
#include "Bitmap.h"
#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <cstdio>
#include <cstring>

namespace detail {
    BitmapMutex::BitmapMutex() = default;
    
    void BitmapMutex::lock() noexcept {
        mutex_.lock();
    }
    
    void BitmapMutex::unlock() noexcept {
        mutex_.unlock();
    }
    
    bool BitmapMutex::try_lock() noexcept {
        return mutex_.try_lock();
    }
}

#define BITS_PER_BYTE 8

using namespace std;

Bitmap::Bitmap(size_t number_of_bits, unsigned char initial_bit_value) {
    if (number_of_bits > MAX_BITS) {
        throw std::invalid_argument("number_of_bits exceeds MAX_BITS");
    }
    number_of_bits_ = number_of_bits;
    number_of_words_ = (number_of_bits / (sizeof(size_t) * BITS_PER_BYTE)) +
                       ((number_of_bits % (sizeof(size_t) * BITS_PER_BYTE)) == 0 ? 0 : 1);
    size_t initial_word_value = (initial_bit_value ? ~0 : 0);
    bitmap_.fill(0);
    fill(bitmap_.begin(), bitmap_.begin() + number_of_words_, initial_word_value);
}

unsigned char Bitmap::bitValue(size_t bit_number) const {
    size_t word_num = bit_number / (sizeof(size_t) * BITS_PER_BYTE);
    size_t word_bit_number = bit_number % (sizeof(size_t) * BITS_PER_BYTE);
    size_t check_bit = (static_cast<size_t>(1)) << word_bit_number;
    return (bitmap_[word_num] & check_bit) ? 1 : 0;
}

bool Bitmap::isSet(size_t bit_number) const {
    return bitValue(bit_number) == 1;
}

void Bitmap::setBitTo(size_t bit_number, unsigned char new_bit_value) {
    if (bit_number >= number_of_bits_) {
        throw std::out_of_range("bit_number exceeds bitmap size");
    }
    size_t word_num = bit_number / (sizeof(size_t) * BITS_PER_BYTE);
    size_t word_bit_number = bit_number % (sizeof(size_t) * BITS_PER_BYTE);
    size_t bit_mask = (static_cast<size_t>(1)) << word_bit_number;
    if (new_bit_value) {
        bitmap_[word_num] |= bit_mask;
    } else {
        bitmap_[word_num] &= ~bit_mask;
    }
}

int Bitmap::getAndOptionallyClearFirstOne(bool do_clear) const {
    std::lock_guard<detail::BitmapMutex> guard(in_use_);
    size_t result = INVALID_BIT_NUMBER;
    const size_t bits_per_word = sizeof(size_t) * BITS_PER_BYTE;

    for (size_t word_num = 0; word_num < number_of_words_; ++word_num) {
        size_t check_word = bitmap_[word_num];
        if (check_word != 0) {
            size_t base_offset = word_num * bits_per_word;
            if (base_offset >= number_of_bits_) {
                break;
            }
            size_t bits_remaining = number_of_bits_ - base_offset;
            size_t bits_to_check = (std::min)(bits_per_word, bits_remaining);

            for (size_t bit_num = 0; bit_num < bits_to_check; ++bit_num) {
                size_t check_bit = (static_cast<size_t>(1)) << bit_num;
                if (check_bit & check_word) {
                    result = base_offset + bit_num;
                    if (do_clear) {
                        const_cast<Bitmap*>(this)->bitmap_[word_num] &= (~check_bit);
                    }
                    break;
                }
            }
        }
        if (result != INVALID_BIT_NUMBER) {
            break;
        }
    }
    return (result == INVALID_BIT_NUMBER ? -1 : static_cast<int>(result));
}

int Bitmap::getFirstOne() const {
    return getAndOptionallyClearFirstOne(false);
}

int Bitmap::getAndClearFirstOne() {
    return const_cast<Bitmap*>(this)->getAndOptionallyClearFirstOne(true);
}

int Bitmap::getAndOptionallySetFirstZero(bool do_set) {
    std::lock_guard<detail::BitmapMutex> guard(in_use_);
    size_t result = INVALID_BIT_NUMBER;
    const size_t bits_per_word = sizeof(size_t) * BITS_PER_BYTE;

    for (size_t word_num = 0; word_num < number_of_words_; ++word_num) {
        size_t check_word = bitmap_[word_num];
        if (check_word != (size_t)~0) {
            size_t base_offset = word_num * bits_per_word;
            if (base_offset >= number_of_bits_) {
                break;
            }
            size_t bits_remaining = number_of_bits_ - base_offset;
            size_t bits_to_check = (std::min)(bits_per_word, bits_remaining);

            for (size_t bit_num = 0; bit_num < bits_to_check; ++bit_num) {
                size_t check_bit = (static_cast<size_t>(1)) << bit_num;
                if ((check_bit & check_word) == 0) {
                    result = base_offset + bit_num;
                    if (do_set) {
                        bitmap_[word_num] |= check_bit;
                    }
                    break;
                }
            }
        }
        if (result != INVALID_BIT_NUMBER) {
            break;
        }
    }
    return (result == INVALID_BIT_NUMBER ? -1 : static_cast<int>(result));
}

int Bitmap::getFirstZero() const {
    return const_cast<Bitmap*>(this)->getAndOptionallySetFirstZero(false);
}

int Bitmap::getAndSetFirstZero() {
    return getAndOptionallySetFirstZero(true);
}

unsigned char Bitmap::testAndSet(size_t bit_number) {
    std::lock_guard<detail::BitmapMutex> guard(in_use_);
    unsigned char result = bitValue(bit_number);
    if (result != 1) {
        setBitTo(bit_number, 1);
    }
    return result;
}

unsigned char Bitmap::testAndClear(size_t bit_number) {
    std::lock_guard<detail::BitmapMutex> guard(in_use_);
    unsigned char result = bitValue(bit_number);
    if (result != 0) {
        setBitTo(bit_number, 0);
    }
    return result;
}

int Bitmap::countOnes() {
    std::lock_guard<detail::BitmapMutex> guard(in_use_);
    int number_of_ones = 0;
    const size_t bits_per_word = sizeof(size_t) * BITS_PER_BYTE;

    for (size_t word_num = 0; word_num < number_of_words_; ++word_num) {
        size_t check_word = bitmap_[word_num];
        size_t base_offset = word_num * bits_per_word;

        if (base_offset >= number_of_bits_) {
            break;
        }

        size_t bits_remaining = number_of_bits_ - base_offset;
        size_t bits_to_check = (std::min)(bits_per_word, bits_remaining);

        for (size_t bit_num = 0; bit_num < bits_to_check; ++bit_num) {
            size_t check_bit = (static_cast<size_t>(1)) << bit_num;
            if (check_bit & check_word) {
                ++number_of_ones;
            }
        }
    }
    return number_of_ones;
}

int Bitmap::countZeroes() {
    return number_of_bits_ - countOnes();
}

std::string Bitmap::asHexString() const {
    std::lock_guard<detail::BitmapMutex> guard(in_use_);
    std::string result;
#if defined(_WIN64) || defined(__x86_64__) || defined(__ppc64__)
    // 64-bit: 16 hex digits.
    const char* fmt = "%016zx";
#else
    // 32-bit: 8 hex digits.
    const char* fmt = "%08zx";
#endif
    for (size_t i = 0; i < number_of_words_; ++i) {
        char buf[32];
        // Use sprintf_s with the fixed format.
        sprintf_s(buf, sizeof(buf), fmt, bitmap_[i]);
        result.insert(0, buf);
    }
    return result;
}

std::string Bitmap::asBinaryString() const {
    std::lock_guard<detail::BitmapMutex> guard(in_use_);
    char buf[1001];
    if (number_of_bits_ > 1000) {
        return "(too many bits for binary string)";
    }
    size_t i;
    for (i = 0; i < number_of_bits_; ++i) {
        buf[i] = bitValue(i) + '0';
    }
    buf[i] = 0;
    return std::string(buf);
}
