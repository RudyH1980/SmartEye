/**
 * @file rtc_constants.hpp
 * @brief RTC time conversion and formatting constants
 */

#pragma once

#include <cstdint>

namespace rtc_services {

/**
 * @brief Time format constants
 */
namespace TimeFormat {
/**
 * @brief struct tm year base
 * @details POSIX struct tm stores years as "years since 1900"
 *          e.g., tm_year = 124 represents year 2024
 */
constexpr int TM_YEAR_BASE = 1900;

/**
 * @brief struct tm month base
 * @details POSIX struct tm stores months as 0-11 (0=January, 11=December)
 */
constexpr int TM_MONTH_BASE = 0;

/**
 * @brief Day of week values
 * @details POSIX wday: 0=Sunday, 1=Monday, ..., 6=Saturday
 */
constexpr int SUNDAY = 0;
constexpr int SATURDAY = 6;
constexpr int DAYS_IN_WEEK = 7;
}  // namespace TimeFormat

/**
 * @brief Zeller's Congruence algorithm constants
 * @details Used for calculating day of week from date
 * @see getDayOfWeek() implementation
 *
 * Formula: h = (q + ⌊13(m+1)/5⌋ + K + ⌊K/4⌋ + ⌊J/4⌋ - 2J) mod 7
 * Where:
 *   - h = day of week (0=Saturday, 1=Sunday, 2=Monday, ..., 6=Friday)
 *   - q = day of month
 *   - m = month (3=March, 4=April, ..., 14=February)
 *   - K = year of century (year % 100)
 *   - J = zero-based century (⌊year/100⌋)
 *
 * @note January and February are counted as months 13 and 14 of the previous year
 */
namespace ZellerCongruence {
/// Month adjustment coefficient: 13(m+1)/5
constexpr int MONTH_COEFFICIENT = 13;

/// Divisor for month calculation
constexpr int MONTH_DIVISOR = 5;

/// Century divisor
constexpr int CENTURY_DIVISOR = 100;

/// Year divisor for leap year calculation
constexpr int LEAP_YEAR_DIVISOR = 4;

/// Century adjustment factor
constexpr int CENTURY_FACTOR = 2;

/// Week modulo
constexpr int WEEK_MODULO = 7;

/**
 * @brief Day of week offset adjustment
 * @details Zeller's returns 0=Saturday, but we want 0=Sunday
 *          Adjustment: (zeller_result + 6) % 7
 */
constexpr int DAY_OFFSET = 6;
}  // namespace ZellerCongruence

}  // namespace rtc_services
