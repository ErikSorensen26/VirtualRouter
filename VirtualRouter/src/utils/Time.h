/**
 * @file Time.h
 * @brief Formatted wall-clock time retrieval for log output and display.
 */

#ifndef TIME_H
#define TIME_H

#include <string>

namespace utils
{

/**
 * @brief Produces formatted wall-clock time strings for display and logging.
 * @ingroup UTILS
 *
 * Wraps platform clock calls and converts the result to a human-readable
 * string according to the configured format options. Callers set public
 * fields before calling getTime() to choose between 12/24-hour and
 * millisecond precision.
 *
 * ## Architectural Role
 * A lightweight formatting helper with no shared state between instances.
 * Each subsystem that needs timestamps constructs its own instance and
 * configures it independently.
 *
 * ## Lifecycle & Ownership
 * Stateless beyond its configuration flags; safe to construct on the stack
 * or as a member. No teardown is required.
 */
class DoTime
{
public:
    /**
     * @brief Constructs a DoTime with 12-hour format and millisecond precision enabled.
     */
    DoTime();

    /**
     * @brief Returns the current wall-clock time as a formatted string.
     *
     * The exact format depends on @ref milTime and @ref includeMilliseconds.
     * When NTP is unavailable the system clock is used as-is with no
     * correction applied.
     *
     * @return Formatted time string (e.g. "14:30:05.123" or "2:30:05 PM").
     */
    std::string getTime();

    bool milTime = false;            ///< When true, formats time in 24-hour (military) notation.
    bool includeMilliseconds = true; ///< When true, appends sub-second precision to the output.

private:
    bool ntp = false; ///< Reserved for future NTP-synchronized clock source; unused today.
};

} // namespace utils

#endif // TIME_H
