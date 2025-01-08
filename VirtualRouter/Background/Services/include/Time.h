// Time.h

#ifndef TIME_H
#define TIME_H

#include <string>

// Class to handle time formatting and retrieval.
class DoTime {
public:
    // Constructor: Initializes the DoTime object.
    DoTime();
    
    // Returns the current time as a string based on object settings.
    std::string getTime();

    bool milTime = false;             // Indicates if time should be in military format.
    bool includeMilliseconds = true; // Indicates if milliseconds should be included in the time string.
private:
    bool ntp = false; // Indicates if Network Time Protocol is used.
};

// Returns the number of seconds since the epoch (January 1, 1970).
double secondsSinceEpoch();

#endif // TIME_H
