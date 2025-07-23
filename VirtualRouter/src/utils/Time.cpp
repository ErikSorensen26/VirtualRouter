#include <ctime>  
#include <string> 
#include <chrono> 
#include <Time.h> 
#include <Functions.h>

DoTime::DoTime() // Default constructor for the DoTime class.
{
    // Nothing implemented
}

// Returns the current time formatted as a string based on object settings.
// Includes milliseconds if 'includeMilliseconds' is true.
std::string DoTime::getTime() 
{
    std::time_t now = std::time(nullptr); // Get current time as std::time_t.
    std::tm localTime; // Structure to hold local time.

    #ifdef _MSC_VER
    localtime_s(&localTime, &now); // Secure version of localtime for MSVC.
    #else
    localtime_r(&now, &localTime); // Thread-safe version of localtime for non-MSVC.
    #endif

    // Get the current time in milliseconds since the epoch and compute remainder.
    auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count() % 1000;

    char buffer[80]; // Buffer to hold formatted time.
    if (milTime) 
    {
        std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &localTime); // Format time in 24-hour format.
    } 
    else 
    {
        std::strftime(buffer, sizeof(buffer), "%I:%M:%S", &localTime); // Format time in 12-hour format.
    }

    char milli_buffer[8]; // Buffer to hold milliseconds.
    #ifdef _MSC_VER
    sprintf_s(milli_buffer, ".%03d", static_cast<int>(milliseconds)); // Secure sprintf for MSVC.
    #else
    snprintf(milli_buffer, sizeof(milli_buffer), ".%03d", static_cast<int>(milliseconds)); // Safe snprintf for other compilers.
    #endif

    char bufferEnd[80]; // Buffer to hold end of formatted time string.
    if (milTime) 
    {
        std::strftime(bufferEnd, sizeof(bufferEnd), " %Z %a %b %d %Y", &localTime); // Format date and time with timezone in military format.
    } 
    else 
    {
        std::strftime(bufferEnd, sizeof(bufferEnd), " %p %Z %a %b %d %Y", &localTime); // Format date and time with AM/PM in civilian format.
    }

    std::string formattedTime(buffer); // Initialize formatted time string with the base time.
    if (includeMilliseconds) 
    {
        formattedTime += milli_buffer; // Append milliseconds if the option is set.
    }
    formattedTime += bufferEnd; // Append the full date and time string.

    return formattedTime; // Return the complete formatted time string.
}
