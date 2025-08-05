// Logger.hpp

#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <iostream>
#include <string>
#include <sstream>

struct Logger
{
    static void info(const std::string& msg)
    {
        std::cout << "[*] " << msg << std::endl;
    }

    static void warn(const std::string& msg)
    {
        std::cerr << "[!] " << msg << std::endl;
    }

    static void error(const std::string& msg)
    {
        std::cerr << "[ERROR] " << msg << std::endl;
    }

    template<typename T>
    static std::string toBase(T value, int base) {
        std::ostringstream oss;
        if (base == 10) oss << value;
        else if (base == 16) oss << "0x" << std::hex << value;
        else if (base == 256)
        {
            const unsigned char* ptr = reinterpret_cast<const unsigned char*>(&value);
            oss << "[ ";
            for (size_t i = 0; i < sizeof(T); ++i)
            {
                oss << (int)ptr[i] << " ";
            }
            oss << "]";
        } else oss << value;
        return oss.str();
    }
};

#endif // LOGGER_HPP
