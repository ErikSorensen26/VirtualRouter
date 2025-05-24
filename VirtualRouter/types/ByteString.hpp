// ByteString.hpp

#ifndef BYTE_STRING_HPP
#define BYTE_STRING_HPP

#include <cstdint>
#include <cstring>
#include <string>
#include <stdexcept>
#include <algorithm>
#include <iostream>
#include <string>

class ByteString;
class ByteRef;

class Byte
{
public:
    uint8_t value;

    Byte() : value(0) {}
    Byte(uint8_t val) : value(val) {}
    Byte(int val) : value(static_cast<uint8_t>(val)) {}
    Byte(char val) : value(static_cast<uint8_t>(val)) {}
    Byte(bool val) : value(val ? 1 : 0) {}
    inline const uint8_t* raw() const noexcept { return reinterpret_cast<const uint8_t*>(value); }

    operator uint8_t() const { return value; }
    operator char() const { return static_cast<char>(value); }
    operator int() const { return static_cast<int>(value); }
    operator bool() const { return value != 0; }
    operator ByteRef();

    inline std::string toHex(bool prefix = false) const 
    {
        static const char hex[] = "0123456789ABCDEF";
        std::string result = prefix ? "0x" : "";
        result += hex[(value >> 4) & 0xF];
        result += hex[value & 0xF];
        return result;
    }

    inline std::string toBin() const 
    {
        std::string result(8, '0');
        for (int i = 0; i < 8; ++i)
            if (value & (1 << (7 - i)))
                result[i] = '1';
        return result;
    }

    inline std::string toDec() const { return std::to_string(value); }

    // Bit access
    inline bool getBit(uint8_t bit) const { return (value >> bit) & 0x1; }
    inline void setBit(uint8_t bit) { value |= (1 << bit); }
    inline void clearBit(uint8_t bit) { value &= ~(1 << bit); }
    inline void toggleBit(uint8_t bit) { value ^= (1 << bit); }

    inline bool isBitSet(uint8_t bit) const 
    {
        if (bit > 7) throw std::out_of_range("Byte::isBitSet: bit index must be in [0,7]");
        return (value >> bit) & 0x1;
    }

    // Bit shifts
    Byte operator<<(int shift) const { return Byte(value << shift); }
    Byte operator>>(int shift) const { return Byte(value >> shift); }

    Byte& operator<<=(int shift) { value <<= shift; return *this; }
    Byte& operator>>=(int shift) { value >>= shift; return *this; }

    // Bitwize ops
    Byte operator|(Byte other) const { return Byte(value | other.value); }
    Byte operator&(Byte other) const { return Byte(value & other.value); }
    Byte operator^(Byte other) const { return Byte(value ^ other.value); }

    Byte operator|(uint8_t other) const { return Byte(value | other); }
    Byte operator&(uint8_t other) const { return Byte(value & other); }
    Byte operator^(uint8_t other) const { return Byte(value ^ other); }

    Byte operator|(int other) const { return Byte(value | other); }
    Byte operator&(int other) const { return Byte(value & other); }
    Byte operator^(int other) const { return Byte(value ^ other); }

    Byte operator~() const { return Byte(~value); }

    Byte& operator|=(Byte other) { value |= other.value; return *this; }
    Byte& operator&=(Byte other) { value &= other.value; return *this; }

    Byte& operator|=(uint8_t other) { value |= other; return *this; }
    Byte& operator&=(uint8_t other) { value &= other; return *this; }

    Byte& operator|=(int other) { value |= other; return *this; }
    Byte& operator&=(int other) { value &= other; return *this; }
    
    // Arithetic ops
    Byte operator+(Byte other) const { return Byte(value + other.value); }
    Byte operator-(Byte other) const { return Byte(value - other.value); }
    Byte operator*(Byte other) const { return Byte(value * other.value); }
    Byte operator/(Byte other) const { return Byte(value / other.value); }

    Byte operator+(uint8_t other) const { return Byte(value + other); }
    Byte operator-(uint8_t other) const { return Byte(value - other); }
    Byte operator*(uint8_t other) const { return Byte(value * other); }
    Byte operator/(uint8_t other) const { return Byte(value / other); }

    Byte operator+(int other) const { return Byte(value + other); }
    Byte operator-(int other) const { return Byte(value - other); }
    Byte operator*(int other) const { return Byte(value * other); }
    Byte operator/(int other) const { return Byte(value / other); }

    Byte& operator+=(Byte other) { value += other.value; return *this; }
    Byte& operator-=(Byte other) { value -= other.value; return *this; }
    Byte& operator*=(Byte other) { value *= other.value; return *this; }
    Byte& operator/=(Byte other) { value /= other.value; return *this; }

    Byte& operator+=(uint8_t other) { value += other; return *this; }
    Byte& operator-=(uint8_t other) { value -= other; return *this; }
    Byte& operator*=(uint8_t other) { value *= other; return *this; }
    Byte& operator/=(uint8_t other) { value /= other; return *this; }

    Byte& operator+=(int other) { value += other; return *this; }
    Byte& operator-=(int other) { value -= other; return *this; }
    Byte& operator*=(int other) { value *= other; return *this; }
    Byte& operator/=(int other) { value /= other; return *this; }

    // Equality
    bool operator==(const Byte& other) const { return value == other.value; }
    bool operator!=(const Byte& other) const { return value != other.value; }

    bool operator==(uint8_t other) const { return value == other; }
    bool operator!=(uint8_t other) const { return value != other; }

    bool operator==(int other) const { return value == other; }
    bool operator!=(int other) const { return value != other; }

    // Relational
    bool operator<(const Byte& other) const { return value < other.value; }
    bool operator>(const Byte& other) const { return value > other.value; }
    bool operator<=(const Byte& other) const { return value <= other.value; }
    bool operator>=(const Byte& other) const { return value >= other.value; }

    bool operator<(uint8_t other) const { return value < other; }
    bool operator>(uint8_t other) const { return value > other; }
    bool operator<=(uint8_t other) const { return value <= other; }
    bool operator>=(uint8_t other) const { return value >= other; }

    bool operator<(int other) const { return value < static_cast<uint8_t>(other); }
    bool operator>(int other) const { return value > static_cast<uint8_t>(other); }
    bool operator<=(int other) const { return value <= static_cast<uint8_t>(other); }
    bool operator>=(int other) const { return value >= static_cast<uint8_t>(other); }

    friend std::ostream& operator<<(std::ostream& os, const Byte& b)
    {
        os << static_cast<int>(b.value);
        return os;
    }
};

class ByteRef
{
    uint8_t& ref_;
public:
    ByteRef(uint8_t& r) : ref_(r) {}

    // Copy out a Byte
    operator Byte() const { return Byte(ref_); }
    operator uint8_t() const { return ref_; }

    void operator=(Byte b) { ref_ = b.value; }
    void operator=(uint8_t v) { ref_ = v; }
    void operator=(int v) { ref_ = static_cast<uint8_t>(v); }

    inline std::string toHex(bool prefix = false) const 
    {
        static const char hex[] = "0123456789ABCDEF";
        std::string result = prefix ? "0x" : "";
        result += hex[(ref_ >> 4) & 0xF];
        result += hex[ref_ & 0xF];
        return result;
    }

    inline std::string toBin() const 
    {
        std::string result(8, '0');
        for (int i = 0; i < 8; ++i)
            if (ref_ & (1 << (7 - i)))
                result[i] = '1';
        return result;
    }

    inline std::string toDec() const { return std::to_string(ref_); }

    // Bit access
    inline bool getBit(uint8_t bit) const { return (ref_ >> bit) & 0x1; }
    inline void setBit(uint8_t bit) { ref_ |= (1 << bit); }
    inline void clearBit(uint8_t bit) { ref_ &= ~(1 << bit); }
    inline void toggleBit(uint8_t bit) { ref_ ^= (1 << bit); }

    inline bool isBitSet(uint8_t bit) const 
    {
        if (bit > 7) throw std::out_of_range("Byte::isBitSet: bit index must be in [0,7]");
        return (ref_ >> bit) & 0x1;
    }

    // Bitwize ops
    Byte operator|(Byte other) const { return Byte(ref_ | other.value); }
    Byte operator&(Byte other) const { return Byte(ref_ & other.value); }
    Byte operator^(Byte other) const { return Byte(ref_ ^ other.value); }

    Byte operator|(ByteRef other) const { return Byte(ref_ | other.ref_); }
    Byte operator&(ByteRef other) const { return Byte(ref_ & other.ref_); }
    Byte operator^(ByteRef other) const { return Byte(ref_ ^ other.ref_); }

    Byte operator|(uint8_t other) const { return Byte(ref_ | other); }
    Byte operator&(uint8_t other) const { return Byte(ref_ & other); }
    Byte operator^(uint8_t other) const { return Byte(ref_ ^ other); }

    Byte operator|(int other) const { return Byte(ref_ | other); }
    Byte operator&(int other) const { return Byte(ref_ & other); }
    Byte operator^(int other) const { return Byte(ref_ ^ other); }

    Byte operator~() const { return Byte(~ref_); }

    // Bit shifts
    void operator<<=(int shift) { ref_ <<= shift; }
    void operator>>=(int shift) { ref_ >>= shift; }

    void operator|=(Byte other) { ref_ |= other.value; }
    void operator&=(Byte other) { ref_ &= other.value; }

    void operator|=(ByteRef other) { ref_ |= other.ref_; }
    void operator&=(ByteRef other) { ref_ &= other.ref_; }

    void operator|=(uint8_t other) { ref_ |= other; }
    void operator&=(uint8_t other) { ref_ &= other; }

    void operator|=(int other) { ref_ |= other; }
    void operator&=(int other) { ref_ &= other; }
    
    // Arithetic ops
    Byte operator+(Byte other) const { return Byte(ref_ + other.value); }
    Byte operator-(Byte other) const { return Byte(ref_ - other.value); }
    Byte operator*(Byte other) const { return Byte(ref_ * other.value); }
    Byte operator/(Byte other) const { return Byte(ref_ / other.value); }

    Byte operator+(ByteRef other) const { return Byte(ref_ + other.ref_); }
    Byte operator-(ByteRef other) const { return Byte(ref_ - other.ref_); }
    Byte operator*(ByteRef other) const { return Byte(ref_ * other.ref_); }
    Byte operator/(ByteRef other) const { return Byte(ref_ / other.ref_); }

    Byte operator+(uint8_t other) const { return Byte(ref_ + other); }
    Byte operator-(uint8_t other) const { return Byte(ref_ - other); }
    Byte operator*(uint8_t other) const { return Byte(ref_ * other); }
    Byte operator/(uint8_t other) const { return Byte(ref_ / other); }

    Byte operator+(int other) const { return Byte(ref_ + other); }
    Byte operator-(int other) const { return Byte(ref_ - other); }
    Byte operator*(int other) const { return Byte(ref_ * other); }
    Byte operator/(int other) const { return Byte(ref_ / other); }
    
    void operator+=(Byte other) { ref_ += other.value; }
    void operator-=(Byte other) { ref_ -= other.value; }
    void operator*=(Byte other) { ref_ *= other.value; }
    void operator/=(Byte other) { ref_ /= other.value; }

    void operator+=(ByteRef other) { ref_ += other.ref_; }
    void operator-=(ByteRef other) { ref_ -= other.ref_; }
    void operator*=(ByteRef other) { ref_ *= other.ref_; }
    void operator/=(ByteRef other) { ref_ /= other.ref_; }

    void operator+=(uint8_t other) { ref_ += other; }
    void operator-=(uint8_t other) { ref_ -= other; }
    void operator*=(uint8_t other) { ref_ *= other; }
    void operator/=(uint8_t other) { ref_ /= other; }

    void operator+=(int other) { ref_ += other; }
    void operator-=(int other) { ref_ -= other; }
    void operator*=(int other) { ref_ *= other; }
    void operator/=(int other) { ref_ /= other; }

    // Equality
    bool operator==(const Byte& other) const { return ref_ == other.value; }
    bool operator!=(const Byte& other) const { return ref_ != other.value; }

    bool operator==(const ByteRef& other) const { return ref_ == other.ref_; }
    bool operator!=(const ByteRef& other) const { return ref_ != other.ref_; }

    bool operator==(uint8_t other) const { return ref_ == other; }
    bool operator!=(uint8_t other) const { return ref_ != other; }

    bool operator==(int other) const { return ref_ == other; }
    bool operator!=(int other) const { return ref_ != other; }

    // Relational
    bool operator<(const Byte& other) const { return ref_ < other.value; }
    bool operator>(const Byte& other) const { return ref_ > other.value; }
    bool operator<=(const Byte& other) const { return ref_ <= other.value; }
    bool operator>=(const Byte& other) const { return ref_ >= other.value; }

    bool operator<(const ByteRef& other) const { return ref_ < other.ref_; }
    bool operator>(const ByteRef& other) const { return ref_ > other.ref_; }
    bool operator<=(const ByteRef& other) const { return ref_ <= other.ref_; }
    bool operator>=(const ByteRef& other) const { return ref_ >= other.ref_; }

    bool operator<(uint8_t other) const { return ref_ < other; }
    bool operator>(uint8_t other) const { return ref_ > other; }
    bool operator<=(uint8_t other) const { return ref_ <= other; }
    bool operator>=(uint8_t other) const { return ref_ >= other; }

    bool operator<(int other) const { return ref_ < static_cast<uint8_t>(other); }
    bool operator>(int other) const { return ref_ > static_cast<uint8_t>(other); }
    bool operator<=(int other) const { return ref_ <= static_cast<uint8_t>(other); }
    bool operator>=(int other) const { return ref_ >= static_cast<uint8_t>(other); }
};

inline Byte::operator ByteRef() { return ByteRef(value); }

class ByteString 
{
public:
    static constexpr size_t SBO_BUFFER_SIZE = 16;

    inline ByteString() noexcept : size_(0), capacity_(SBO_BUFFER_SIZE), is_sbo_(true) {
        // Initialize SBO buffer to zero once at construction for cleanliness
        std::memset(sbo_buffer_, 0, SBO_BUFFER_SIZE);
    }

    inline ByteString(const ByteString& other) : size_(other.size_), is_sbo_(other.is_sbo_) {
        if (is_sbo_) {
            capacity_ = SBO_BUFFER_SIZE;
            std::memcpy(sbo_buffer_, other.sbo_buffer_, size_);
        } else {
            capacity_ = other.capacity_;
            data_ptr_ = static_cast<uint8_t*>(std::malloc(capacity_));
            if (!data_ptr_) throw std::bad_alloc();
            std::memcpy(data_ptr_, other.data_ptr_, size_);
        }
    }

    inline ByteString(const void* cstr, size_t size) {
        initialize(reinterpret_cast<const Byte*>(cstr), size);
    }
    inline ByteString(const char* cstr, size_t size) {
        initialize(reinterpret_cast<const Byte*>(cstr), size);
    }

    inline ByteString(ByteString&& other) noexcept 
        : size_(other.size_), capacity_(other.capacity_), is_sbo_(other.is_sbo_) {
        if (is_sbo_) {
            std::memcpy(sbo_buffer_, other.sbo_buffer_, size_);
        } else {
            data_ptr_ = other.data_ptr_;
            other.data_ptr_ = nullptr;
        }
        other.size_ = 0;
        other.is_sbo_ = true;
        other.capacity_ = SBO_BUFFER_SIZE;
        std::memset(other.sbo_buffer_, 0, SBO_BUFFER_SIZE);
    }

    inline ByteString(const char* cstr) {
        size_t len = std::strlen(cstr);
        initialize(reinterpret_cast<const Byte*>(cstr), len);
    }

    inline ByteString(const std::string& str) {
        initialize(reinterpret_cast<const Byte*>(str.data()), str.size());
    }

    inline ByteString(const std::string& str, size_t size) {
        if (size <= SBO_BUFFER_SIZE) {
            is_sbo_ = true;
            capacity_ = SBO_BUFFER_SIZE;
            if (size <= str.size()) {
                size_ = size;
                std::memcpy(sbo_buffer_, str.data(), size_);
            } else {
                size_ = size;
                std::memcpy(sbo_buffer_, str.data(), str.size());
                std::memset(sbo_buffer_ + str.size(), 0, size - str.size());
            }
        } else {
            is_sbo_ = false;
            capacity_ = std::max(size * 2, str.size() > size ? str.size() * 2 : size * 2);
            data_ptr_ = static_cast<uint8_t*>(std::malloc(capacity_));
            if (!data_ptr_) throw std::bad_alloc();
            if (size <= str.size()) {
                size_ = size;
                std::memcpy(data_ptr_, str.data(), size_);
            } else {
                size_ = size;
                std::memcpy(data_ptr_, str.data(), str.size());
                std::memset(data_ptr_ + str.size(), 0, size - str.size());
            }
        }
    }

    inline ByteString(size_t length, Byte value) {
        if (length <= SBO_BUFFER_SIZE) {
            is_sbo_ = true;
            capacity_ = SBO_BUFFER_SIZE;
            size_ = length;
            std::memset(sbo_buffer_, value, size_);
        } else {
            is_sbo_ = false;
            capacity_ = length * 2;
            data_ptr_ = static_cast<uint8_t*>(std::malloc(capacity_));
            if (!data_ptr_) throw std::bad_alloc();
            std::memset(data_ptr_, value, length);
            size_ = length;
        }
    }

    inline ByteString(std::initializer_list<Byte> init_list) {
        initialize(init_list.begin(), init_list.size());
    }

    inline ~ByteString() {
        if (!is_sbo_ && data_ptr_) {
            std::free(data_ptr_);
        }
    }

    inline ByteString& operator=(const ByteString& other) {
        if (this == &other) return *this;
        clearInternal();
        size_ = other.size_;
        is_sbo_ = other.is_sbo_;
        if (is_sbo_) {
            capacity_ = SBO_BUFFER_SIZE;
            std::memcpy(sbo_buffer_, other.sbo_buffer_, size_);
        } else {
            capacity_ = other.capacity_;
            data_ptr_ = static_cast<uint8_t*>(std::malloc(capacity_));
            if (!data_ptr_) throw std::bad_alloc();
            std::memcpy(data_ptr_, other.data_ptr_, size_);
        }
        return *this;
    }

    inline ByteString& operator=(ByteString&& other) {
        if (this == &other) return *this;
        clearInternal();
        size_ = other.size_;
        capacity_ = other.capacity_;
        is_sbo_ = other.is_sbo_;
        if (is_sbo_) {
            std::memcpy(sbo_buffer_, other.sbo_buffer_, size_);
        } else {
            data_ptr_ = other.data_ptr_;
            other.data_ptr_ = nullptr;
        }
        other.size_ = 0;
        other.is_sbo_ = true;
        other.capacity_ = SBO_BUFFER_SIZE;
        std::memset(other.sbo_buffer_, 0, SBO_BUFFER_SIZE);
        return *this;
    }

    inline size_t size() const noexcept { return size_; }
    inline bool empty() const noexcept { return size_ == 0; }

    inline ByteRef operator[](size_t index) {
        if (index >= size_) throw std::out_of_range("ByteString::operator[]");
        uint8_t& byteRef = *(is_sbo_ ? &sbo_buffer_[index] : &data_ptr_[index]);
        return ByteRef(byteRef);
    }

    inline Byte operator[](size_t index) const {
        if (index >= size_) throw std::out_of_range("ByteString::operator[]: index out of range");
        return Byte(is_sbo_ ? sbo_buffer_[index] : data_ptr_[index]);
    }

    inline ByteRef at(size_t index) {
        if (index >= size_) throw std::out_of_range("ByteString::at");
        return (*this)[index];
    }
    inline Byte at(size_t index) const {
        if (index >= size_) throw std::out_of_range("ByteString::at (const)");
        return (*this)[index];
    }

    int compare(const ByteString& other) const {
        size_t min_size = std::min(size_, other.size_);
        int cmp = std::memcmp(data(), other.data(), min_size);
        if (cmp != 0) return cmp;
        return (size_ < other.size_) ? -1 : (size_ > other.size_) ? 1 : 0;
    }

    inline uint8_t* begin() noexcept { return is_sbo_ ? sbo_buffer_ : data_ptr_; }
    inline const uint8_t* begin() const noexcept { return is_sbo_ ? sbo_buffer_ : data_ptr_; }
    inline const uint8_t* cbegin() const noexcept { return begin(); }

    inline uint8_t* end() noexcept { return begin() + size_; }
    inline const uint8_t* end() const noexcept { return begin() + size_; }
    inline const uint8_t* cend() const noexcept { return end(); }

    inline std::reverse_iterator<uint8_t*> rbegin() noexcept { return std::reverse_iterator(end()); }
    inline std::reverse_iterator<const uint8_t*> rbegin() const noexcept { return std::reverse_iterator(end()); }
    inline std::reverse_iterator<uint8_t*> rend() noexcept { return std::reverse_iterator(begin()); }
    inline std::reverse_iterator<const uint8_t*> rend() const noexcept { return std::reverse_iterator(begin()); }

    inline ByteRef front() { return (*this)[0]; }
    inline ByteRef back() { return (*this)[size_ - 1]; }
    inline Byte front() const { return (*this)[0]; }
    inline Byte back() const { return (*this)[size_ - 1]; }

    bool start_with(const ByteString& prefix) const {
        return size_ >= prefix.size() && std::memcmp(data(), prefix.data(), prefix.size()) == 0;
    }

    bool ends_with(const ByteString& suffix) const {
        return size_ >= suffix.size() &&
            std::memcmp(data() + size_ - suffix.size(), suffix.data(), suffix.size()) == 0;
    }

    bool contains(const ByteString& sub) const {
        return find(sub) != std::string::npos;
    }

    inline void clear() noexcept {
        size_ = 0;
        // Don't zero anything out unnecessarily. The user must trust this.
        // is_sbo_ and capacity_ remain unchanged if still SBO.
        if (!is_sbo_) {
            // Keep allocation for future use (like std::string)
        }
    }

    inline void reserve(size_t new_cap) {
        if (new_cap <= SBO_BUFFER_SIZE) {
            // No action needed
            return;
        }
        if (is_sbo_) {
            // Switch to heap
            uint8_t* new_data = static_cast<uint8_t*>(std::malloc(new_cap));
            if (!new_data) throw std::bad_alloc();
            std::memcpy(new_data, sbo_buffer_, size_);
            is_sbo_ = false;
            capacity_ = new_cap;
            data_ptr_ = new_data;
        } else {
            if (new_cap > capacity_) {
                uint8_t* new_data = static_cast<uint8_t*>(std::realloc(data_ptr_, new_cap));
                if (!new_data) throw std::bad_alloc();
                data_ptr_ = new_data;
                capacity_ = new_cap;
            }
        }
    }

    inline void resize(size_t new_size, Byte fill_value = 0) {
        // If the size is unchanged, do nothing
        if (new_size == size_) return;

        if (new_size < size_) {
            // ---------------------
            // Shrinking
            // ---------------------
            size_ = new_size;
            // If we had a heap buffer but now the size is small enough,
            // switch back to SBO
            if (!is_sbo_ && size_ <= SBO_BUFFER_SIZE) {
                // Copy the active data into the SBO buffer
                std::memcpy(sbo_buffer_, data_ptr_, size_);
                // Free the old heap buffer
                std::free(data_ptr_);
                data_ptr_ = nullptr;
                // Mark that we're using SBO now
                is_sbo_ = true;
                capacity_ = SBO_BUFFER_SIZE;
                std::memset(sbo_buffer_ + size_, 0, SBO_BUFFER_SIZE - size_);
            }
        } else {
            // ---------------------
            // Expanding
            // ---------------------
            // Check if we already have enough capacity
            if (new_size <= (is_sbo_ ? SBO_BUFFER_SIZE : capacity_)) {
                // We can just fill the extra space with fill_value
                if (is_sbo_) {
                    std::memset(sbo_buffer_ + size_, fill_value, new_size - size_);
                } else {
                    std::memset(data_ptr_ + size_, fill_value, new_size - size_);
                }
                size_ = new_size;
            } else {
                // We must allocate or reallocate
                size_t new_cap = (is_sbo_ ? SBO_BUFFER_SIZE : capacity_);
                // Double until we have at least new_size
                while (new_cap < new_size) {
                    new_cap *= 2;
                }

                if (is_sbo_) {
                    // We were in SBO, need to move to heap
                    uint8_t* new_data = static_cast<uint8_t*>(std::malloc(new_cap));
                    if (!new_data) throw std::bad_alloc();

                    // Copy existing SBO data
                    std::memcpy(new_data, sbo_buffer_, size_);
                    // Fill the new area
                    std::memset(new_data + size_, fill_value, new_size - size_);

                    data_ptr_ = new_data;
                    capacity_ = new_cap;
                    is_sbo_ = false;
                    size_ = new_size;
                } else {
                    // We already have a heap buffer, just reallocate
                    uint8_t* new_data = static_cast<uint8_t*>(std::realloc(data_ptr_, new_cap));
                    if (!new_data) throw std::bad_alloc();

                    data_ptr_ = new_data;
                    capacity_ = new_cap;
                    // Fill the newly added space
                    std::memset(data_ptr_ + size_, fill_value, new_size - size_);
                    size_ = new_size;
                }
            }
        }
    }

    inline void push_back(Byte b) noexcept {
        if (is_sbo_) {
            if (size_ < SBO_BUFFER_SIZE) {
                sbo_buffer_[size_++] = b;
            } else {
                // Switch to heap
                size_t new_cap = SBO_BUFFER_SIZE * 2;
                uint8_t* new_data = static_cast<uint8_t*>(std::malloc(new_cap));
                std::memcpy(new_data, sbo_buffer_, size_);
                new_data[size_++] = b;
                is_sbo_ = false;
                data_ptr_ = new_data;
                capacity_ = new_cap;
            }
        } else {
            if (size_ >= capacity_) {
                size_t new_cap = capacity_ * 2;
                uint8_t* new_data = static_cast<uint8_t*>(std::realloc(data_ptr_, new_cap));
                data_ptr_ = new_data;
                capacity_ = new_cap;
            }
            data_ptr_[size_++] = b;
        }
    }

    inline void append(const ByteString& other) {
        if (other.empty()) return;
        ensureCapacityForAppend(other.size_);
        // Insert data
        if (is_sbo_) {
            std::memcpy(sbo_buffer_ + size_, other.data(), other.size_);
            size_ += other.size_;
        } else {
            std::memcpy(data_ptr_ + size_, other.data(), other.size_);
            size_ += other.size_;
        }
    }

    inline void append(const std::string& str) {
        if (str.empty()) return;
        ensureCapacityForAppend(str.size());
        const Byte* src = reinterpret_cast<const Byte*>(str.data());
        if (is_sbo_) {
            std::memcpy(sbo_buffer_ + size_, src, str.size());
            size_ += str.size();
        } else {
            std::memcpy(data_ptr_ + size_, src, str.size());
            size_ += str.size();
        }
    }

    inline ByteString substr(size_t pos, size_t len = std::string::npos) const {
        if (pos > size_) {
            throw std::out_of_range("ByteString::substr: pos out of range");
        }
        size_t rlen = (len == std::string::npos || pos + len > size_) ? size_ - pos : len;
        ByteString result;
        if (rlen <= SBO_BUFFER_SIZE) {
            result.is_sbo_ = true;
            result.size_ = rlen;
            result.capacity_ = SBO_BUFFER_SIZE;
            std::memcpy(result.sbo_buffer_, data() + pos, rlen);
        } else {
            result.is_sbo_ = false;
            result.size_ = rlen;
            result.capacity_ = rlen * 2;
            result.data_ptr_ = static_cast<uint8_t*>(std::malloc(result.capacity_));
            if (!result.data_ptr_) throw std::bad_alloc();
            std::memcpy(result.data_ptr_, data() + pos, rlen);
        }
        return result;
    }

    inline std::string toString() const {
        return std::string(reinterpret_cast<const char*>(data()), size_);
    }

    // Highly efficient toHex using a lookup table
    inline std::string toHex() const {
        static const char hex_table[] = "0123456789ABCDEF";
        std::string result;
        result.reserve(size_ * 2);
        for (size_t i = 0; i < size_; ++i) {
            uint8_t v = data()[i];
            result.push_back(hex_table[v >> 4]);
            result.push_back(hex_table[v & 0xF]);
        }
        return result;
    }

    inline uint64_t toUint64(size_t offset = 0) const {
        if (size_ - offset > 8) throw std::overflow_error("too big for uint64_t");
        uint64_t result = 0;
        for (size_t i = offset; i < size_; ++i) {
            result = (result << 8) | (*this)[i].value;
        }
        return result;
    }

    inline uint32_t toUint32(size_t offset = 0) const {
        if (size_ - offset > 4) throw std::overflow_error("too big for uint32_t");
        uint32_t result = 0;
        for (size_t i = offset; i < size_; ++i) {
            result = (result << 8) | (*this)[i].value;
        }
        return result;
    }

    inline uint32_t toUint16(size_t offset = 0) const {
        if (size_ - offset > 2) throw std::overflow_error("too big for uint16_t");
        uint16_t result = 0;
        for (size_t i = offset; i < size_; ++i) {
            result = (result << 8) | (*this)[i].value;
        }
        return result;
    }

    bool getBit(size_t bitIndex) const {
        size_t byte = bitIndex / 8;
        size_t bit = bitIndex % 8;
        if (byte >= size_) throw std::out_of_range("bitIndex");
        return (*this)[byte].getBit(7 - bit);
    }

    int toInt() const {
        return static_cast<int>(toUint64());
    }

    inline size_t find(const ByteString& pattern, size_t pos = 0) const {
        if (pattern.size_ == 0) return pos;
        if (pattern.size_ > size_) return std::string::npos;

        const uint8_t* start_ptr = data() + pos;
        const uint8_t* end_ptr = data() + size_;
        const uint8_t* result = std::search(start_ptr, end_ptr, pattern.begin(), pattern.end());
        if (result != end_ptr)
            return static_cast<size_t>(result - data());
        return std::string::npos;
    }

    //inline size_t replace(const ByteString& target, const ByteString& replacement) {
        //if (target.empty()) return 0;
        //size_t count = 0;
        //size_t pos = 0;
        //for (;;) {
            //pos = find(target, pos);
            //if (pos == std::string::npos) break;
            //erase(pos, target.size_);
            //insert(pos, replacement);
            //pos += replacement.size_;
            //++count;
        //}
        //return count;
    //}

    inline ByteString& replace(size_t pos, size_t len, const ByteString& replacement) {
        if (pos > size_) {
            throw std::out_of_range("ByteString::replace: position out of range");
        }

        // Ensure 'len' does not exceed the remaining time
        len = std::min(len, size_ - pos);

        size_t replacement_len = replacement.size();
        size_t new_size = size_ - len + replacement_len;

        // Ensure sufficient capacity
        if (new_size > capacity_) {
            reserve(std::max(new_size, capacity_ * 2));
        }

        // Move the tall data if sizes differ
        if (replacement_len != len) {
            std::memmove(
                begin() + pos + replacement_len, // Destination
                begin() + pos + len,             // Source
                size_ - pos - len                // Number of bytes to move
            );
        }

        // Copy replacement data
        std::memcpy(begin() + pos, replacement.data(), replacement_len);

        // Update size
        size_ = new_size;

        return *this;
    }

    inline void insert(size_t pos, const ByteString& other) {
        if (pos > size_) throw std::out_of_range("ByteString::insert: pos out of range");
        ensureCapacityForAppend(other.size_);
        if (is_sbo_) {
            // Shift data right
            std::memmove(sbo_buffer_ + pos + other.size_, sbo_buffer_ + pos, size_ - pos);
            // Insert
            std::memcpy(sbo_buffer_ + pos, other.data(), other.size_);
            size_ += other.size_;
        } else {
            // Shift data right in heap buffer
            std::memmove(data_ptr_ + pos + other.size_, data_ptr_ + pos, size_ - pos);
            std::memcpy(data_ptr_ + pos, other.data(), other.size_);
            size_ += other.size_;
        }
    }

    inline void erase(size_t pos, size_t len) {
        if (pos > size_) throw std::out_of_range("ByteString::erase: pos out of range");
        size_t rlen = (pos + len > size_) ? size_ - pos : len;
        if (is_sbo_) {
            std::memmove(sbo_buffer_ + pos, sbo_buffer_ + pos + rlen, size_ - pos - rlen);
            size_ -= rlen;
        } else {
            std::memmove(data_ptr_ + pos, data_ptr_ + pos + rlen, size_ - pos - rlen);
            size_ -= rlen;
            if (size_ <= SBO_BUFFER_SIZE) {
                // Switch back to SBO
                std::memcpy(sbo_buffer_, data_ptr_, size_);
                std::free(data_ptr_);
                data_ptr_ = nullptr;
                is_sbo_ = true;
                capacity_ = SBO_BUFFER_SIZE;
            }
        }
    }

    inline ByteString operator+(const ByteString& other) const {
        ByteString result(*this);
        result.append(other);
        return result;
    }

    inline ByteString& operator+=(const ByteString& other) {
        append(other);
        return *this;
    }

    inline bool operator==(const ByteString& other) const {
        if (size_ != other.size_) return false;
        return (std::memcmp(data(), other.data(), size_) == 0);
    }

    inline bool operator!=(const ByteString& other) const {
        return !(*this == other);
    }

    inline bool operator<(const ByteString& other) const {
        size_t min_size = std::min(size_, other.size_);
        int cmp = std::memcmp(data(), other.data(), min_size);
        if (cmp < 0) return true;
        if (cmp > 0) return false;
        return size_ < other.size_;
    }

    friend std::ostream& operator<<(std::ostream& os, const ByteString& bs) {
        for (size_t i = 0; i < bs.size_; ++i) {
            os << static_cast<char>(bs.data()[i]);
        }
        return os;
    }

    friend std::istream& operator>>(std::istream& is, ByteString& bs) {
        std::string temp;
        is >> temp;
        bs = ByteString(temp);
        return is;
    }

    inline const uint8_t* data() const {
        return is_sbo_ ? sbo_buffer_ : data_ptr_;
    }

    inline uint8_t* data() {
        return is_sbo_ ? sbo_buffer_ : data_ptr_;
    }

private:
    inline void initialize(const Byte* input, size_t len) {
        size_ = len;
        if (len <= SBO_BUFFER_SIZE) {
            is_sbo_ = true;
            capacity_ = SBO_BUFFER_SIZE;
            std::memcpy(sbo_buffer_, input, len);
            // No zeroing out needed beyond size_
        } else {
            is_sbo_ = false;
            capacity_ = len * 2;
            data_ptr_ = static_cast<uint8_t*>(std::malloc(capacity_));
            if (!data_ptr_) throw std::bad_alloc();
            std::memcpy(data_ptr_, input, len);
        }
    }

    inline void ensureCapacityForAppend(size_t extra) {
        size_t required = size_ + extra;
        if (required <= (is_sbo_ ? SBO_BUFFER_SIZE : capacity_)) return;
        // Need to grow
        if (is_sbo_) {
            size_t new_cap = std::max(required, SBO_BUFFER_SIZE * 2);
            uint8_t* new_data = static_cast<uint8_t*>(std::malloc(new_cap));
            if (!new_data) throw std::bad_alloc();
            std::memcpy(new_data, sbo_buffer_, size_);
            data_ptr_ = new_data;
            capacity_ = new_cap;
            is_sbo_ = false;
        } else {
            size_t new_cap = capacity_;
            while (new_cap < required) new_cap *= 2; // double until we have enough
            uint8_t* new_data = static_cast<uint8_t*>(std::realloc(data_ptr_, new_cap));
            if (!new_data) throw std::bad_alloc();
            data_ptr_ = new_data;
            capacity_ = new_cap;
        }
    }

    inline void clearInternal() {
        if (!is_sbo_ && data_ptr_) {
            std::free(data_ptr_);
            data_ptr_ = nullptr;
        }
        is_sbo_ = true;
        capacity_ = SBO_BUFFER_SIZE;
        size_ = 0;
        std::memset(sbo_buffer_, 0, SBO_BUFFER_SIZE);
    }

    // Members
    union {
        uint8_t sbo_buffer_[SBO_BUFFER_SIZE];
        struct {
            uint8_t* data_ptr_;
        };
    };
    size_t size_;
    size_t capacity_;
    bool is_sbo_;
};

namespace std 
{
    template <>
    struct hash<ByteString> {
        size_t operator()(const ByteString& bs) const noexcept {
            size_t hash = 0;
            const auto* data = bs.begin();
            for (size_t i = 0; i < bs.size(); ++i) {
                hash = hash * 31 + data[i];
            }
            return hash;
        }
    };
}

class ByteSpan 
{
public:
    // Constructors
    ByteSpan() : data_(nullptr), size_(0) {}
    ByteSpan(const uint8_t* data, size_t size) : data_(data), size_(size) {}
    ByteSpan(const ByteString& bs) : data_(bs.data()), size_(bs.size()) {}

    // Element access
    const uint8_t& operator[](size_t index) const {
        if (index >= size_) throw std::out_of_range("ByteSpan::operator[]: index out of range");
        return data_[index];
    }

    const uint8_t* data() const { return data_; }
    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }

    // Slicing
    ByteSpan subspan(size_t offset, size_t count = std::string::npos) const {
        if (offset > size_) throw std::out_of_range("ByteSpan::subspan: offset out of range");
        size_t new_size = (count == std::string::npos || offset + count > size_) ? size_ - offset : count;
        return ByteSpan(data_ + offset, new_size);
    }

    // Conversion to string for compatibility
    std::string toString() const {
        return std::string(reinterpret_cast<const char*>(data_), size_);
    }

    // Comparison operators
    bool operator==(const ByteSpan& other) const {
        return size_ == other.size_ && std::equal(data_, data_ + size_, other.data_);
    }

    bool operator!=(const ByteSpan& other) const { return !(*this == other); }

private:
    const uint8_t* data_;
    size_t size_;
};

class ByteStringSpan 
{
public:
    static const size_t npos = static_cast<size_t>(-1);

    // -------------------------------------------------
    // 1) Constructors
    // -------------------------------------------------

    // Default constructor => invalid span
    ByteStringSpan(ByteString& bs, size_t offset, size_t length)
        : bs_(bs), offset_(offset), length_(length) {
        if (offset_ + length_ > bs_.size()) {
            throw std::out_of_range("ByteStringSpan: range exceeds ByteString size");
        }
    }

    // Construct a span referencing the entire ByteString
    explicit ByteStringSpan(ByteString& bs)
        : ByteStringSpan(bs, 0, bs.size()) {}

    // -------------------------------------------------
    // 2) Assigning Data Into This Span
    // -------------------------------------------------

    // Assign from a ByteString
    ByteStringSpan& operator=(const ByteString& src) {
        if (src.size() != length_) {
            throw std::invalid_argument("ByteStringSpan: source size must match span size.");
        }
        std::memcpy(bs_.data() + offset_, src.data(), length_);
        return *this;
    }

    // Assign from a std::string
    ByteStringSpan& operator=(const std::string& src) {
        if (src.size() != length_) {
            throw std::invalid_argument("ByteStringSpan: source size must match span size.");
        }
        std::memcpy(bs_.data() + offset_, src.data(), length_);
        return *this;
    }

    // -------------------------------------------------
    // 3) Accessing Data
    // -------------------------------------------------

    // Access raw data
    const Byte* data() const {
        return reinterpret_cast<const Byte*>(bs_.data() + offset_);
    }

    Byte* data() {
        return reinterpret_cast<Byte*>(bs_.data() + offset_);
    }

    // Element access
    ByteRef operator[](size_t index) {
        if (index >= length_) {
            throw std::out_of_range("ByteStringSpan::operator[]: index out of range");
        }
        return bs_[offset_ + index];
    }

    const Byte operator[](size_t index) const {
        if (index >= length_) {
            throw std::out_of_range("ByteStringSpan::operator[]: index out of range");
        }
        return bs_[offset_ + index];
    }

    // -------------------------------------------------
    // 4) Span Info
    // -------------------------------------------------

    size_t size() const { return length_; }
    bool empty() const { return length_ == 0; }

    // Get the subrange as a ByteString
    ByteString toByteString() const {
        return bs_.substr(offset_, length_);
    }

    // Get the subrange as a std::string
    std::string toString() const {
        return bs_.substr(offset_, length_).toString();
    }
// Assignment operator for another ByteStringSpan
ByteStringSpan& operator=(const ByteStringSpan& other) {
    if (this == &other) {
        return *this; // Avoid self-assignment
    }
    if (other.bs_ != bs_) {
        throw std::runtime_error("ByteStringSpan: Cannot rebind to a different ByteString.");
    }
    offset_ = other.offset_;
    length_ = other.length_;
    return *this;
}

private:
    ByteString& bs_;  // Reference to the underlying ByteString
    size_t offset_;   // Offset of the span within the ByteString
    size_t length_;   // Length of the span
};

#endif // BYTE_STRING_HPP
