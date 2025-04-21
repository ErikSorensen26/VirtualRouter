#include <memory.h>

template<typename T>
class ExPtr
{
public:
    // Construct with raw pointer
    explicit ExPtr(T* ptr = nullptr): ptr_(ptr), isValid_(true), isOwner_(true) {}

    // Copy constructor (copies the pointer but does not transfer ownership)
    ExPtr(const ExPtr& other) : ptr_(other.ptr_), isValid_(other.isValid_), isOwner_(false) {}

    // Copy assignment operator(copies the pointer but doesn't transfer ownership)
    ExPtr& operator=(const ExPtr& other)
    {
        if (this != &other) 
        {
            ptr_ = other.ptr_;
            isValid_ = other.isValid_;
            isOwner_ = false; // Ensure the copy doesn't have ownership
        }
        return *this;
    }

    // Destructor (only the owner deletes the object)
    ~ExPtr()
    {
        if (isOwner_ && isValid_)
        {
            delete ptr_; // only the owner can delete the object
        }
    }

    // Accessor function to get the pointer
    T* get() const 
    {
        if (!isValid_)
        {
            return nullptr;
        }
        return ptr_;
    }

    // Check if the object is valid (not deleted)
    bool isValid() const
    {
        return isValid_;
    }

    // Manual delete the object (anyone can delete)
    void deleteObject()
    {
        if (isValid_)
        {
            delete ptr_;
            ptr_ = nullptr;
            isValid_ = false;
        }
    }

private:
    T* ptr_;
    bool isValid_;
    bool isOwner_;
};
