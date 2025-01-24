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

class ByteString {
public:
    using byte = uint8_t;

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
            data_ptr_ = static_cast<byte*>(std::malloc(capacity_));
            if (!data_ptr_) throw std::bad_alloc();
            std::memcpy(data_ptr_, other.data_ptr_, size_);
        }
    }

    inline ByteString(const char* cstr, size_t size) {
        initialize(reinterpret_cast<const byte*>(cstr), size);
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
        initialize(reinterpret_cast<const byte*>(cstr), len);
    }

    inline ByteString(const std::string& str) {
        initialize(reinterpret_cast<const byte*>(str.data()), str.size());
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
            data_ptr_ = static_cast<byte*>(std::malloc(capacity_));
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

    inline ByteString(size_t length, byte value) {
        if (length <= SBO_BUFFER_SIZE) {
            is_sbo_ = true;
            capacity_ = SBO_BUFFER_SIZE;
            size_ = length;
            std::memset(sbo_buffer_, value, size_);
        } else {
            is_sbo_ = false;
            capacity_ = length * 2;
            data_ptr_ = static_cast<byte*>(std::malloc(capacity_));
            if (!data_ptr_) throw std::bad_alloc();
            std::memset(data_ptr_, value, length);
            size_ = length;
        }
    }

    inline ByteString(std::initializer_list<byte> init_list) {
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
            data_ptr_ = static_cast<byte*>(std::malloc(capacity_));
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

    inline byte& operator[](size_t index) {
        if (index >= size_) throw std::out_of_range("ByteString::operator[]: index out of range");
        return is_sbo_ ? sbo_buffer_[index] : data_ptr_[index];
    }

    inline const byte& operator[](size_t index) const {
        if (index >= size_) throw std::out_of_range("ByteString::operator[]: index out of range");
        return is_sbo_ ? sbo_buffer_[index] : data_ptr_[index];
    }

    inline byte* begin() noexcept { return is_sbo_ ? sbo_buffer_ : data_ptr_; }
    inline const byte* begin() const noexcept { return is_sbo_ ? sbo_buffer_ : data_ptr_; }
    inline const byte* cbegin() const noexcept { return begin(); }

    inline byte* end() noexcept { return begin() + size_; }
    inline const byte* end() const noexcept { return begin() + size_; }
    inline const byte* cend() const noexcept { return end(); }

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
            byte* new_data = static_cast<byte*>(std::malloc(new_cap));
            if (!new_data) throw std::bad_alloc();
            std::memcpy(new_data, sbo_buffer_, size_);
            is_sbo_ = false;
            capacity_ = new_cap;
            data_ptr_ = new_data;
        } else {
            if (new_cap > capacity_) {
                byte* new_data = static_cast<byte*>(std::realloc(data_ptr_, new_cap));
                if (!new_data) throw std::bad_alloc();
                data_ptr_ = new_data;
                capacity_ = new_cap;
            }
        }
    }

    inline void resize(size_t new_size, byte fill_value = 0) {
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
                    byte* new_data = static_cast<byte*>(std::malloc(new_cap));
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
                    byte* new_data = static_cast<byte*>(std::realloc(data_ptr_, new_cap));
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

    inline void push_back(byte b) noexcept {
        if (is_sbo_) {
            if (size_ < SBO_BUFFER_SIZE) {
                sbo_buffer_[size_++] = b;
            } else {
                // Switch to heap
                size_t new_cap = SBO_BUFFER_SIZE * 2;
                byte* new_data = static_cast<byte*>(std::malloc(new_cap));
                std::memcpy(new_data, sbo_buffer_, size_);
                new_data[size_++] = b;
                is_sbo_ = false;
                data_ptr_ = new_data;
                capacity_ = new_cap;
            }
        } else {
            if (size_ >= capacity_) {
                size_t new_cap = capacity_ * 2;
                byte* new_data = static_cast<byte*>(std::realloc(data_ptr_, new_cap));
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
        const byte* src = reinterpret_cast<const byte*>(str.data());
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
            result.data_ptr_ = static_cast<byte*>(std::malloc(result.capacity_));
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
            byte v = data()[i];
            result.push_back(hex_table[v >> 4]);
            result.push_back(hex_table[v & 0xF]);
        }
        return result;
    }

    inline size_t find(const ByteString& pattern, size_t pos = 0) const {
        if (pattern.size_ == 0) return pos;
        if (pattern.size_ > size_) return std::string::npos;

        const byte* start_ptr = data() + pos;
        const byte* end_ptr = data() + size_;
        const byte* result = std::search(start_ptr, end_ptr, pattern.begin(), pattern.end());
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

    inline ByteString& replace(size_t pos, size_t len, ByteString& replacement) {
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

    inline const byte* data() const {
        return is_sbo_ ? sbo_buffer_ : data_ptr_;
    }

    inline byte* data() {
        return is_sbo_ ? sbo_buffer_ : data_ptr_;
    }

private:
    inline void initialize(const byte* data_ptr, size_t len) {
        size_ = len;
        if (len <= SBO_BUFFER_SIZE) {
            is_sbo_ = true;
            capacity_ = SBO_BUFFER_SIZE;
            std::memcpy(sbo_buffer_, data_ptr, len);
            // No zeroing out needed beyond size_
        } else {
            is_sbo_ = false;
            capacity_ = len * 2;
            data_ptr_ = static_cast<byte*>(std::malloc(capacity_));
            if (!data_ptr_) throw std::bad_alloc();
            std::memcpy(data_ptr_, data_ptr, len);
        }
    }

    inline void ensureCapacityForAppend(size_t extra) {
        size_t required = size_ + extra;
        if (required <= (is_sbo_ ? SBO_BUFFER_SIZE : capacity_)) return;
        // Need to grow
        if (is_sbo_) {
            size_t new_cap = std::max(required, SBO_BUFFER_SIZE * 2);
            byte* new_data = static_cast<byte*>(std::malloc(new_cap));
            if (!new_data) throw std::bad_alloc();
            std::memcpy(new_data, sbo_buffer_, size_);
            data_ptr_ = new_data;
            capacity_ = new_cap;
            is_sbo_ = false;
        } else {
            if (required > capacity_) {
                size_t new_cap = capacity_;
                while (new_cap < required) {
                    new_cap *= 2; // double until we have enough
                }
                byte* new_data = static_cast<byte*>(std::realloc(data_ptr_, new_cap));
                if (!new_data) throw std::bad_alloc();
                data_ptr_ = new_data;
                capacity_ = new_cap;
            }
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
        byte sbo_buffer_[SBO_BUFFER_SIZE];
        struct {
            byte* data_ptr_;
        };
    };
    size_t size_;
    size_t capacity_;
    bool is_sbo_;
};

namespace std {
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

class ByteSpan {
public:
    using byte = uint8_t;

    // Constructors
    ByteSpan() : data_(nullptr), size_(0) {}
    ByteSpan(const byte* data, size_t size) : data_(data), size_(size) {}
    ByteSpan(const ByteString& bs) : data_(bs.data()), size_(bs.size()) {}

    // Element access
    const byte& operator[](size_t index) const {
        if (index >= size_) throw std::out_of_range("ByteSpan::operator[]: index out of range");
        return data_[index];
    }

    const byte* data() const { return data_; }
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
    const byte* data_;
    size_t size_;
};

class ByteStringSpan {
public:
    using byte = uint8_t;
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
    const byte* data() const {
        return reinterpret_cast<const byte*>(bs_.data() + offset_);
    }

    byte* data() {
        return reinterpret_cast<byte*>(bs_.data() + offset_);
    }

    // Element access
    byte& operator[](size_t index) {
        if (index >= length_) {
            throw std::out_of_range("ByteStringSpan::operator[]: index out of range");
        }
        return bs_[offset_ + index];
    }

    const byte& operator[](size_t index) const {
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
