/*
SyslogAgent: a syslog agent for Windows
Copyright 2021 Logzilla Corp.
*/

#include "stdafx.h"
#include "Bitmap.h"
#include <algorithm>
#include <mutex>

#define BITS_PER_BYTE 8

using namespace std;

Bitmap::Bitmap(int number_of_bits, unsigned char initial_bit_value) {
    number_of_bits_ = number_of_bits;
    number_of_words_ = (number_of_bits / (sizeof(size_t) * BITS_PER_BYTE)) + ((number_of_bits % (sizeof(size_t) * BITS_PER_BYTE)) == 0 ? 0 : 1);
    size_t initial_word_value = (initial_bit_value ? ~0 : 0);
    bitmap_.resize(number_of_words_);
    fill(bitmap_.begin(), bitmap_.end(), initial_word_value);
}

unsigned char Bitmap::bitValue(int bit_number) const {
    int word_num = bit_number / (sizeof(size_t) * BITS_PER_BYTE);
    size_t word_bit_number = bit_number % (sizeof(size_t) * BITS_PER_BYTE);
    size_t check_bit = (static_cast<size_t>(1)) << word_bit_number;
    return (bitmap_[word_num] & check_bit) ? 1 : 0;
}

bool Bitmap::isSet(int bit_number) const {
    return bitValue(bit_number) == 1;
}

void Bitmap::setBitTo(int bit_number, unsigned char new_bit_value) {
    int word_num = bit_number / (sizeof(size_t) * BITS_PER_BYTE);
    int word_bit_number = bit_number % (sizeof(size_t) * BITS_PER_BYTE);
    size_t check_bit = (static_cast<size_t>(1)) << word_bit_number;
    if (new_bit_value) {
        bitmap_[word_num] |= check_bit;
    }
    else {
        bitmap_[word_num] &= (~check_bit);
    }
}

int Bitmap::getAndOptionallyClearFirstOne(bool do_clear) const {
    lock_guard<mutex> lock(in_use_);
    size_t result = INVALID_BIT_NUMBER;
    const size_t bits_per_word = sizeof(size_t) * BITS_PER_BYTE;
    
    for (int word_num = 0; word_num < number_of_words_; ++word_num) {
        size_t check_word = bitmap_[word_num];
        if (check_word != 0) {
            // Calculate base bit offset safely
            size_t base_offset = static_cast<size_t>(word_num) * bits_per_word;
            
            // Check if we're already past the end
            if (base_offset >= static_cast<size_t>(number_of_bits_)) {
                break;
            }
            
            // Calculate how many bits to check in this word
            size_t bits_remaining = number_of_bits_ - base_offset;
            size_t bits_to_check = std::min<size_t>(bits_per_word, bits_remaining);
            
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
    std::lock_guard<std::mutex> lock(in_use_);
    return getAndOptionallyClearFirstOne(false);
}

int Bitmap::getAndClearFirstOne() {
    std::lock_guard<std::mutex> lock(in_use_);
    return getAndOptionallyClearFirstOne(true);
}

int Bitmap::getAndOptionallySetFirstZero(bool do_set) {
    std::lock_guard<std::mutex> lock(in_use_);
    size_t result = INVALID_BIT_NUMBER;
    const size_t bits_per_word = sizeof(size_t) * BITS_PER_BYTE;
    
    for (int word_num = 0; word_num < number_of_words_; ++word_num) {
        size_t check_word = bitmap_[word_num];
        if (check_word != (size_t)~0) {
            // Calculate base bit offset safely
            size_t base_offset = static_cast<size_t>(word_num) * bits_per_word;
            
            // Check if we're already past the end
            if (base_offset >= static_cast<size_t>(number_of_bits_)) {
                break;
            }
            
            // Calculate how many bits to check in this word
            size_t bits_remaining = number_of_bits_ - base_offset;
            size_t bits_to_check = std::min<size_t>(bits_per_word, bits_remaining);
            
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

unsigned char Bitmap::testAndSet(int bit_number) {
    const std::lock_guard<std::mutex> lock(in_use_);
    int result = bitValue(bit_number);
    if (result != 1) {
        setBitTo(bit_number, 1);
    }
    return result;
}

unsigned char Bitmap::testAndClear(int bit_number) {
    const std::lock_guard<std::mutex> lock(in_use_);
    int result = bitValue(bit_number);
    if (result != 0) {
        setBitTo(bit_number, 0);
    }
    return result;
}

int Bitmap::countOnes() {
    const std::lock_guard<std::mutex> lock(in_use_);
    int number_of_ones = 0;
    const size_t bits_per_word = sizeof(size_t) * BITS_PER_BYTE;

    for (int word_num = 0; word_num < number_of_words_; ++word_num) {
        size_t check_word = bitmap_[word_num];
        size_t base_offset = static_cast<size_t>(word_num) * bits_per_word;
            
        if (base_offset >= static_cast<size_t>(number_of_bits_)) {
            break;
        }
            
        size_t bits_remaining = number_of_bits_ - base_offset;
        size_t bits_to_check = std::min<size_t>(bits_per_word, bits_remaining);

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

string Bitmap::asHexString() const {
    string result;
    char format[10];
    sprintf_s(format, "%%0%dx", (int) sizeof(size_t));
    for (int i = 0; i < number_of_words_; ++i) {
        char buf[32];
        sprintf_s(buf, format, bitmap_[i]);
        result.insert(0, buf);
    }
    return result;
}

string Bitmap::asBinaryString() const {
    char buf[1001];
    if (number_of_bits_ > 1000) {
        return "(too many bits for binary string)";
    }
    int i;
    for (i = 0; i < number_of_bits_; ++i) {
        buf[i] = bitValue(i) + '0';
    }
    buf[i] = 0;
    return string(buf);
}