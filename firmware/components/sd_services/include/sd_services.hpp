/**
 * @file sd_services.hpp
 * @brief SD Card Services - High-level API for SD Card operations
 * @details Provides mount/unmount, file operations, and format support
 *
 * Features:
 * - SDMMC 4-bit mode (fast access)
 * - Power control via TCA9554 P0_3
 * - Mount/unmount with FAT32 support
 * - Card info (size, type, speed, free space)
 * - File operations (read, write, delete, list)
 * - FAT32 format with progress callback
 * - Thread-safe operations
 *
 * Hardware: SD Card slot with SDMMC interface
 */

#pragma once

#include <ctime>
#include <functional>
#include <string>
#include <vector>
#include "esp_err.h"
#include "esp_io_expander.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

namespace sd_services {

// ============================================================================
// DATA STRUCTURES
// ============================================================================

/**
 * @brief SD Card information
 */
struct SDCardInfo {
    uint64_t totalBytes;  // Total capacity in bytes
    uint64_t freeBytes;   // Free space in bytes
    uint64_t usedBytes;   // Used space in bytes
    const char *type;     // Card type (SDSC, SDHC, SDXC)
    uint32_t speedMHz;    // Clock speed in MHz
    bool isMounted;       // Mount status
};

/**
 * @brief File entry for directory listing
 */
struct FileEntry {
    std::string name;  // File/folder name
    uint64_t size;     // File size in bytes
    bool isDirectory;  // True if directory
    time_t modTime;    // Last modification time
};

/**
 * @brief Progress callback for format operation
 * @param percent Progress percentage (0-100)
 */
using ProgressCallback = std::function<void(uint8_t percent)>;

/**
 * @brief Card detect callback (called when SD card inserted/removed)
 * @param mounted true if card mounted, false if removed
 */
using CardDetectCallback = std::function<void(bool mounted)>;

// ============================================================================
// INITIALIZATION & POWER
// ============================================================================

/**
 * @brief Initialize SD card with power control and mount
 * @param mountPoint Mount point path (default: "/sdcard")
 * @param maxFiles Maximum number of open files (default: 5)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t initSD(const char *mountPoint = "/sdcard", size_t maxFiles = 5);

/**
 * @brief Deinitialize SD card (unmount and power off)
 * @return ESP_OK on success
 */
esp_err_t deinitSD();

/**
 * @brief Check if SD card is mounted
 * @return true if mounted
 */
bool isMounted();

/**
 * @brief Power on SD card via TCA9554
 * @return ESP_OK on success
 */
esp_err_t powerOn();

/**
 * @brief Power off SD card via TCA9554
 * @return ESP_OK on success
 */
esp_err_t powerOff();

/**
 * @brief Start auto-detect task (polls for SD card every 2 seconds)
 * @param callback Optional callback for mount/unmount events
 * @return ESP_OK on success
 * @note Power efficient: ~0.5-1mA during polling, stops in sleep mode
 */
esp_err_t startAutoDetect(const CardDetectCallback &callback = nullptr);

/**
 * @brief Stop auto-detect task
 * @return ESP_OK on success
 */
esp_err_t stopAutoDetect();

/**
 * @brief Check if auto-detect is running
 * @return true if running
 */
bool isAutoDetectRunning();

// ============================================================================
// CARD INFORMATION
// ============================================================================

/**
 * @brief Get SD card information
 * @param info Pointer to store card info
 * @return ESP_OK on success
 */
esp_err_t getCardInfo(SDCardInfo *info);

/**
 * @brief Get mount point path
 * @return Mount point string (e.g., "/sdcard")
 */
const char *getMountPoint();

/**
 * @brief Format bytes to human-readable string (B, KB, MB, GB)
 * @param bytes Number of bytes
 * @param buffer Output buffer
 * @param bufferSize Size of output buffer
 * @return Pointer to buffer
 */
const char *formatBytes(uint64_t bytes, char *buffer, size_t bufferSize);

// ============================================================================
// FORMAT
// ============================================================================

/**
 * @brief Format SD card as FAT32
 * @param progressCallback Optional callback for progress updates (0-100%)
 * @return ESP_OK on success
 * @note This will erase all data on the card!
 */
esp_err_t formatSD(const ProgressCallback &progressCallback = nullptr);

// ============================================================================
// FILE OPERATIONS
// ============================================================================

/**
 * @brief List files in a directory
 * @param path Directory path (relative to mount point)
 * @param files Vector to store file entries
 * @return ESP_OK on success
 */
esp_err_t listFiles(const char *path, std::vector<FileEntry> &files);

/**
 * @brief Check if file exists
 * @param path File path (relative to mount point)
 * @return true if file exists
 */
bool fileExists(const char *path);

/**
 * @brief Read file contents
 * @param path File path (relative to mount point)
 * @param buffer Buffer to store file contents
 * @param bufferSize Size of buffer
 * @param bytesRead Pointer to store number of bytes read
 * @return ESP_OK on success
 */
esp_err_t readFile(const char *path, char *buffer, size_t bufferSize, size_t *bytesRead);

/**
 * @brief Write data to file (overwrites existing)
 * @param path File path (relative to mount point)
 * @param data Data to write
 * @param size Size of data
 * @return ESP_OK on success
 */
esp_err_t writeFile(const char *path, const char *data, size_t size);

/**
 * @brief Append data to file
 * @param path File path (relative to mount point)
 * @param data Data to append
 * @param size Size of data
 * @return ESP_OK on success
 */
esp_err_t appendFile(const char *path, const char *data, size_t size);

/**
 * @brief Delete file
 * @param path File path (relative to mount point)
 * @return ESP_OK on success
 */
esp_err_t deleteFile(const char *path);

/**
 * @brief Get file size
 * @param path File path (relative to mount point)
 * @param size Pointer to store file size
 * @return ESP_OK on success
 */
esp_err_t getFileSize(const char *path, size_t *size);

// ============================================================================
// LOGGING HELPERS
// ============================================================================

/**
 * @brief Create a new log file with timestamp
 * @param prefix Log file prefix (e.g., "imu_log")
 * @param outPath Buffer to store created file path
 * @param pathSize Size of outPath buffer
 * @return ESP_OK on success
 */
esp_err_t createLogFile(const char *prefix, char *outPath, size_t pathSize);

/**
 * @brief Append log entry to file
 * @param path Log file path
 * @param entry Log entry string
 * @return ESP_OK on success
 */
esp_err_t appendLog(const char *path, const char *entry);

// ============================================================================
// INTERNAL (Called by board_drivers)
// ============================================================================

/**
 * @brief Set IO expander handle (internal - called by board_drivers)
 * @param ioExpander IO expander handle from TCA9554
 */
void setIOExpanderHandle(esp_io_expander_handle_t ioExpander);

}  // namespace sd_services
