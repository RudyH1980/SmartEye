/**
 * @file sd_constants.hpp
 * @brief SD card configuration and threshold constants
 */

#pragma once

#include <cstdint>

namespace sd_services {

/**
 * @brief SD card power management timing
 */
namespace PowerTiming {
/// Power stabilization delay after SD card power-on (milliseconds)
constexpr uint32_t POWER_ON_DELAY_MS = 300;
}  // namespace PowerTiming

/**
 * @brief FAT filesystem configuration
 */
namespace FATConfig {
/// Allocation unit size for better large file performance (16KB clusters)
constexpr size_t ALLOCATION_UNIT_SIZE = 16 * 1024;
}  // namespace FATConfig

/**
 * @brief SD card type detection thresholds (bytes)
 */
namespace CardTypeThresholds {
/// Minimum size for SDXC classification (32GB)
constexpr uint64_t SDXC_MIN_SIZE = 32ULL * 1024 * 1024 * 1024;

/// Minimum size for SDHC classification (2GB)
constexpr uint64_t SDHC_MIN_SIZE = 2ULL * 1024 * 1024 * 1024;

// Below SDHC_MIN_SIZE is classified as SDSC (Standard Capacity)
}  // namespace CardTypeThresholds

/**
 * @brief File size display formatting
 */
namespace FileSizeFormat {
/// Bytes per kilobyte
constexpr double BYTES_PER_KB = 1024.0;

/// Bytes per megabyte
constexpr double BYTES_PER_MB = BYTES_PER_KB * 1024.0;

/// Bytes per gigabyte
constexpr double BYTES_PER_GB = BYTES_PER_MB * 1024.0;
}  // namespace FileSizeFormat

/**
 * @brief Auto-detection task configuration
 */
namespace AutoDetectConfig {
/// Task stack size (bytes)
constexpr size_t TASK_STACK_SIZE = 8192;

/// Task priority
constexpr uint8_t TASK_PRIORITY = 3;

/// SD card polling interval (milliseconds)
constexpr uint32_t POLL_INTERVAL_MS = 30000;  // 30 seconds

/// Task cleanup timeout (milliseconds)
constexpr uint32_t CLEANUP_TIMEOUT_MS = 3000;

/// Cleanup retry interval (milliseconds)
constexpr uint32_t CLEANUP_RETRY_MS = 100;

/// Maximum cleanup retries
constexpr uint32_t CLEANUP_MAX_RETRIES = CLEANUP_TIMEOUT_MS / CLEANUP_RETRY_MS;
}  // namespace AutoDetectConfig

}  // namespace sd_services
