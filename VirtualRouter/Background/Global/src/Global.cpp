#include <Global.h>

std::shared_mutex Global::instanceMutex;
Global* Global::instance = nullptr;

/**
 * @brief Constructor to initiate the Global class
 *
 * This constructor is for initiating the global class storing all of the
 * global variables
 */
Global::Global() : hostname("router"), ipv6Enabled(/*false*/true) {}
