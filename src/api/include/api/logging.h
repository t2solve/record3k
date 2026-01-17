#pragma once
#include <trantor/utils/Logger.h>
#include <cstdlib>

// Lightweight verbose logging macro - controlled by API_VERBOSE environment variable
// Usage: export API_VERBOSE=1 to enable verbose logging
#define LOG_VERBOSE_ENABLED (std::getenv("API_VERBOSE") != nullptr)
#define LOG_VERBOSE if (LOG_VERBOSE_ENABLED) LOG_INFO
