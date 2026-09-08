/**
 * @file sd_services.cpp
 * @brief SD Card Services Implementation
 */

#include "sd_services.hpp"
#include "sd_constants.hpp"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <cerrno>
#include <cstring>
#include <ctime>

#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "ff.h"  // FatFs for f_mkfs
#include "sdmmc_cmd.h"

#include "board_config.hpp"
#include "board_drivers.hpp"

static const char *tag = "SD_Services";

namespace sd_services {

using namespace sd_services;

// ============================================================================
// PRIVATE STATE
// ============================================================================

static sdmmc_card_t *gCard = nullptr;
static char gMountPoint[32] = "/sdcard";
static bool gIsMounted = false;
static esp_io_expander_handle_t gIoExpander = nullptr;

// Auto-detect state
static TaskHandle_t gAutoDetectTask = nullptr;
static CardDetectCallback gDetectCallback = nullptr;
static bool gAutoDetectRunning = false;
static bool gWasMounted = false;  // Track previous mount state

// ============================================================================
// POWER CONTROL
// ============================================================================

esp_err_t powerOn() {
    if (gIoExpander == nullptr) {
        ESP_LOGE(tag, "IO Expander not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Set SD power pin HIGH (P0_3)
    esp_err_t ret = esp_io_expander_set_level(gIoExpander, BoardConfig::TCA9554_SD_POWER_MASK, 1);

    if (ret != ESP_OK) {
        ESP_LOGE(tag, "Failed to power on SD card: %s", esp_err_to_name(ret));
        return ret;
    }

    // Wait for SD card to stabilize (increased delay for better reliability)
    vTaskDelay(pdMS_TO_TICKS(PowerTiming::POWER_ON_DELAY_MS));

    ESP_LOGI(tag, "SD card powered on");
    return ESP_OK;
}

esp_err_t powerOff() {
    if (gIoExpander == nullptr) {
        ESP_LOGE(tag, "IO Expander not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Set SD power pin LOW (P0_3)
    esp_err_t ret = esp_io_expander_set_level(gIoExpander, BoardConfig::TCA9554_SD_POWER_MASK, 0);

    if (ret != ESP_OK) {
        ESP_LOGE(tag, "Failed to power off SD card: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(tag, "SD card powered off");
    return ESP_OK;
}

// ============================================================================
// INITIALIZATION
// ============================================================================

esp_err_t initSD(const char *mountPoint, size_t maxFiles) {
    if (gIsMounted) {
        ESP_LOGW(tag, "SD card already mounted");
        return ESP_OK;
    }

    ESP_LOGI(tag, "Initializing SD card...");

    // Save mount point
    strncpy(gMountPoint, mountPoint, sizeof(gMountPoint) - 1);

    // Power on SD card (IO expander handle set by board_drivers during init)
    esp_err_t ret = powerOn();
    if (ret != ESP_OK) {
        return ret;
    }

    // Mount configuration
    esp_vfs_fat_sdmmc_mount_config_t mountConfig = {
        // ESP-IDF's FATFS has no exFAT support, so a factory-formatted SDXC
        // card (>32GB ships as exFAT) must be rewritten as FAT32 on first use.
        .format_if_mount_failed = true,
        .max_files = static_cast<int>(maxFiles),
        .allocation_unit_size = FATConfig::ALLOCATION_UNIT_SIZE,  // 16KB allocation unit
        .disk_status_check_enable = false,
        .use_one_fat = false};

    // SDMMC host configuration (1-bit mode - per Waveshare demo)
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;  // 20MHz default frequency

    // SD slot configuration (1-bit mode only)
    sdmmc_slot_config_t slotConfig = SDMMC_SLOT_CONFIG_DEFAULT();
    slotConfig.width = 1;                                 // 1-bit mode (more reliable than 4-bit)
    slotConfig.clk = BoardConfig::SD_PIN_CLK;             // GPIO_2
    slotConfig.cmd = BoardConfig::SD_PIN_CMD;             // GPIO_1
    slotConfig.d0 = BoardConfig::SD_PIN_D0;               // GPIO_42
    slotConfig.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;  // Enable internal pull-ups

    // Mount SD card
    ret = esp_vfs_fat_sdmmc_mount(gMountPoint, &host, &slotConfig, &mountConfig, &gCard);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(tag, "Failed to mount filesystem. Format the card or check connection.");
        } else {
            ESP_LOGE(tag, "Failed to initialize SD card: %s", esp_err_to_name(ret));
        }
        powerOff();
        return ret;
    }

    gIsMounted = true;

    // Print card info
    ESP_LOGI(tag, "SD card mounted successfully at %s", gMountPoint);
    sdmmc_card_print_info(stdout, gCard);

    return ESP_OK;
}

esp_err_t deinitSD() {
    if (!gIsMounted) {
        ESP_LOGW(tag, "SD card not mounted");
        return ESP_OK;
    }

    ESP_LOGI(tag, "Unmounting SD card...");

    // Unmount
    esp_err_t ret = esp_vfs_fat_sdcard_unmount(gMountPoint, gCard);
    if (ret != ESP_OK) {
        ESP_LOGE(tag, "Failed to unmount SD card: %s", esp_err_to_name(ret));
        return ret;
    }

    gCard = nullptr;
    gIsMounted = false;

    // Power off
    ret = powerOff();
    if (ret != ESP_OK) {
        ESP_LOGE(tag, "Failed to power off SD card: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(tag, "SD card unmounted and powered off");
    return ESP_OK;
}

bool isMounted() {
    return gIsMounted;
}

const char *getMountPoint() {
    return gMountPoint;
}

const char *formatBytes(uint64_t bytes, char *buffer, size_t bufferSize) {
    if (buffer == nullptr || bufferSize == 0) {
        return "";
    }

    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int unitIndex = 0;
    auto size = (double)bytes;

    while (size >= FileSizeFormat::BYTES_PER_KB && unitIndex < 4) {
        size /= FileSizeFormat::BYTES_PER_KB;
        unitIndex++;
    }

    if (unitIndex == 0) {
        // Bytes - no decimal
        snprintf(buffer, bufferSize, "%llu %s", bytes, units[unitIndex]);
    } else if (size >= 100.0) {
        // >= 100 - no decimal places
        snprintf(buffer, bufferSize, "%.0f %s", size, units[unitIndex]);
    } else if (size >= 10.0) {
        // >= 10 - one decimal place
        snprintf(buffer, bufferSize, "%.1f %s", size, units[unitIndex]);
    } else {
        // < 10 - two decimal places
        snprintf(buffer, bufferSize, "%.2f %s", size, units[unitIndex]);
    }

    return buffer;
}

// ============================================================================
// CARD INFORMATION
// ============================================================================

esp_err_t getCardInfo(SDCardInfo *info) {
    if (!gIsMounted || gCard == nullptr || info == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    // Get card capacity
    uint64_t cardSizeBytes = ((uint64_t)gCard->csd.capacity) * gCard->csd.sector_size;
    info->totalBytes = cardSizeBytes;

    // Get filesystem info for free space using mount point
    FATFS *fs = nullptr;
    DWORD freClust = 0;

    // f_getfree needs the mount point path (e.g., "/sdcard")
    FRESULT res = f_getfree(gMountPoint, &freClust, &fs);

    if (res == FR_OK && fs != nullptr) {
        // Calculate total and free space
        // n_fatent = total number of clusters + 2
        // csize = cluster size in sectors
        // ssize = sector size in bytes
        uint64_t totalClusters = fs->n_fatent - 2;
        uint64_t clusterSize = (uint64_t)fs->csize * fs->ssize;

        uint64_t totalBytes = totalClusters * clusterSize;
        uint64_t freeBytes = (uint64_t)freClust * clusterSize;

        info->freeBytes = freeBytes;
        info->usedBytes = (totalBytes > freeBytes) ? (totalBytes - freeBytes) : 0;

        ESP_LOGI(
            tag,
            "Cluster size: %llu bytes (%u sectors × %u bytes)",
            clusterSize,
            fs->csize,
            fs->ssize
        );
        ESP_LOGI(
            tag,
            "Filesystem stats: total=%llu bytes, free=%llu bytes, used=%llu bytes",
            totalBytes,
            info->freeBytes,
            info->usedBytes
        );
    } else {
        ESP_LOGW(tag, "f_getfree failed with result: %d", res);
        info->freeBytes = 0;
        info->usedBytes = 0;
    }

    // Card type (based on capacity)
    if (cardSizeBytes > CardTypeThresholds::SDXC_MIN_SIZE) {
        info->type = "SDXC";  // > 32GB
    } else if (cardSizeBytes > CardTypeThresholds::SDHC_MIN_SIZE) {
        info->type = "SDHC";  // 2GB - 32GB
    } else {
        info->type = "SDSC";  // <= 2GB
    }

    // Speed
    info->speedMHz = gCard->max_freq_khz / 1000;
    info->isMounted = gIsMounted;

    return ESP_OK;
}

// ============================================================================
// FILE OPERATIONS
// ============================================================================

bool fileExists(const char *path) {
    if (!gIsMounted) {
        return false;
    }

    char fullPath[256];
    snprintf(fullPath, sizeof(fullPath), "%s/%s", gMountPoint, path);

    struct stat st {};
    return (stat(fullPath, &st) == 0);
}

esp_err_t listFiles(const char *path, std::vector<FileEntry> &files) {
    if (!gIsMounted) {
        return ESP_ERR_INVALID_STATE;
    }

    files.clear();

    char fullPath[256];
    snprintf(fullPath, sizeof(fullPath), "%s/%s", gMountPoint, path);

    DIR *dir = opendir(fullPath);
    if (dir == nullptr) {
        ESP_LOGE(tag, "Failed to open directory: %s", fullPath);
        return ESP_FAIL;
    }

    struct dirent *entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
        // Skip "." and ".."
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        FileEntry fileEntry;
        fileEntry.name = entry->d_name;
        fileEntry.isDirectory = (entry->d_type == DT_DIR);

        // Get file stats
        char entryPath[512];
        snprintf(entryPath, sizeof(entryPath), "%s/%s", fullPath, entry->d_name);

        struct stat st {};
        if (stat(entryPath, &st) == 0) {
            fileEntry.size = st.st_size;
            fileEntry.modTime = st.st_mtime;
        } else {
            fileEntry.size = 0;
            fileEntry.modTime = 0;
        }

        files.push_back(fileEntry);
    }

    closedir(dir);
    return ESP_OK;
}

esp_err_t readFile(const char *path, char *buffer, size_t bufferSize, size_t *bytesRead) {
    if (!gIsMounted || buffer == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    char fullPath[256];
    snprintf(fullPath, sizeof(fullPath), "%s/%s", gMountPoint, path);

    FILE *file = fopen(fullPath, "r");
    if (file == nullptr) {
        ESP_LOGE(tag, "Failed to open file for reading: %s", fullPath);
        return ESP_FAIL;
    }

    size_t read = fread(buffer, 1, bufferSize - 1, file);
    buffer[read] = '\0';  // Null-terminate

    if (bytesRead != nullptr) {
        *bytesRead = read;
    }

    fclose(file);
    return ESP_OK;
}

esp_err_t writeFile(const char *path, const char *data, size_t size) {
    if (!gIsMounted || data == nullptr) {
        ESP_LOGE(tag, "writeFile: Invalid args (mounted=%d, data=%p)", gIsMounted, data);
        return ESP_ERR_INVALID_ARG;
    }

    char fullPath[256];
    snprintf(fullPath, sizeof(fullPath), "%s/%s", gMountPoint, path);

    ESP_LOGI(tag, "Attempting to write to: %s", fullPath);
    FILE *file = fopen(fullPath, "w");
    if (file == nullptr) {
        ESP_LOGE(
            tag,
            "Failed to open file for writing: %s (errno=%d: %s)",
            fullPath,
            errno,
            strerror(errno)
        );
        return ESP_FAIL;
    }

    size_t written = fwrite(data, 1, size, file);
    fclose(file);

    if (written != size) {
        ESP_LOGE(tag, "Failed to write all data to file");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t appendFile(const char *path, const char *data, size_t size) {
    if (!gIsMounted || data == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    char fullPath[256];
    snprintf(fullPath, sizeof(fullPath), "%s/%s", gMountPoint, path);

    FILE *file = fopen(fullPath, "a");
    if (file == nullptr) {
        ESP_LOGE(tag, "Failed to open file for appending: %s", fullPath);
        return ESP_FAIL;
    }

    size_t written = fwrite(data, 1, size, file);
    fclose(file);

    if (written != size) {
        ESP_LOGE(tag, "Failed to append all data to file");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t deleteFile(const char *path) {
    if (!gIsMounted) {
        return ESP_ERR_INVALID_STATE;
    }

    char fullPath[256];
    snprintf(fullPath, sizeof(fullPath), "%s/%s", gMountPoint, path);

    if (unlink(fullPath) != 0) {
        ESP_LOGE(tag, "Failed to delete file: %s", fullPath);
        return ESP_FAIL;
    }

    ESP_LOGI(tag, "File deleted: %s", path);
    return ESP_OK;
}

esp_err_t getFileSize(const char *path, size_t *size) {
    if (!gIsMounted || size == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    char fullPath[256];
    snprintf(fullPath, sizeof(fullPath), "%s/%s", gMountPoint, path);

    struct stat st {};
    if (stat(fullPath, &st) != 0) {
        return ESP_FAIL;
    }

    *size = st.st_size;
    return ESP_OK;
}

// ============================================================================
// FORMAT
// ============================================================================

esp_err_t formatSD(const ProgressCallback &progressCallback) {
    if (!gIsMounted || gCard == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGW(tag, "Formatting SD card - ALL DATA WILL BE LOST!");

    // Report progress: 10%
    if (progressCallback) {
        progressCallback(10);
    }

    // Use ESP-IDF's built-in format function (works while mounted)
    ESP_LOGI(tag, "Formatting SD card as FAT32 using esp_vfs_fat_sdcard_format()...");

    // Report progress: 30%
    if (progressCallback) {
        progressCallback(30);
    }

    // Format the card using ESP-IDF's high-level API
    esp_err_t ret = esp_vfs_fat_sdcard_format(gMountPoint, gCard);

    if (ret != ESP_OK) {
        ESP_LOGE(tag, "Format failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(tag, "Format successful!");

    // Report progress: 70%
    if (progressCallback) {
        progressCallback(70);
    }

    // Report progress: 100%
    if (progressCallback) {
        progressCallback(100);
    }

    ESP_LOGI(tag, "Format complete! SD card ready.");
    return ESP_OK;
}

// ============================================================================
// LOGGING HELPERS
// ============================================================================

esp_err_t createLogFile(const char *prefix, char *outPath, size_t pathSize) {
    if (!gIsMounted || prefix == nullptr || outPath == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    // Create filename with timestamp
    time_t now = time(nullptr);
    struct tm *timeinfo = localtime(&now);

    snprintf(
        outPath,
        pathSize,
        "%s_%04d%02d%02d_%02d%02d%02d.csv",
        prefix,
        timeinfo->tm_year + 1900,
        timeinfo->tm_mon + 1,
        timeinfo->tm_mday,
        timeinfo->tm_hour,
        timeinfo->tm_min,
        timeinfo->tm_sec
    );

    // Create empty file
    return writeFile(outPath, "", 0);
}

esp_err_t appendLog(const char *path, const char *entry) {
    if (entry == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    return appendFile(path, entry, strlen(entry));
}

// ============================================================================
// AUTO-DETECT (Flipper Zero inspired - power efficient)
// ============================================================================

// Auto-detect task (only tries once when card is not mounted, then waits)
static void autoDetectTask(void *pvParameters) {
    ESP_LOGI(tag, "Auto-detect task started (single-attempt mode)");

    while (gAutoDetectRunning) {
        bool currentlyMounted = gIsMounted;

        // Only try ONCE if not mounted, don't retry in loop
        if (!currentlyMounted) {
            ESP_LOGI(tag, "Auto-detect: Attempting SD mount (one-time attempt)...");
            esp_err_t ret = initSD(gMountPoint, 5);

            if (ret == ESP_OK) {
                currentlyMounted = true;
                ESP_LOGI(tag, "Auto-detect: SD card mounted successfully!");
            } else {
                ESP_LOGW(tag, "Auto-detect: SD card not found or mount failed");
                // Don't retry automatically - wait for user action or next wake-up
            }
        }

        // Check if mount state changed
        if (currentlyMounted != gWasMounted) {
            gWasMounted = currentlyMounted;

            // Notify callback
            if (gDetectCallback) {
                gDetectCallback(currentlyMounted);
            }

            ESP_LOGI(
                tag,
                "Auto-detect: Mount state changed to %s",
                currentlyMounted ? "MOUNTED" : "NOT MOUNTED"
            );
        }

        // Wait 30 seconds before next check (very infrequent)
        // This allows card insertion to be detected without aggressive polling
        vTaskDelay(pdMS_TO_TICKS(AutoDetectConfig::POLL_INTERVAL_MS));
    }

    ESP_LOGI(tag, "Auto-detect task stopped");
    gAutoDetectTask = nullptr;
    vTaskDelete(nullptr);
}

esp_err_t startAutoDetect(const CardDetectCallback &callback) {
    if (gAutoDetectRunning) {
        ESP_LOGW(tag, "Auto-detect already running");
        return ESP_OK;
    }

    gDetectCallback = callback;
    gAutoDetectRunning = true;
    gWasMounted = gIsMounted;  // Initialize with current state

    // Create low-priority task with adequate stack
    BaseType_t ret = xTaskCreate(
        autoDetectTask,
        "sd_autodetect",
        AutoDetectConfig::TASK_STACK_SIZE,  // 8KB stack (SD operations need more space)
        nullptr,
        AutoDetectConfig::TASK_PRIORITY,  // Low priority (power saving)
        &gAutoDetectTask
    );

    if (ret != pdPASS) {
        gAutoDetectRunning = false;
        gDetectCallback = nullptr;
        ESP_LOGE(tag, "Failed to create auto-detect task");
        return ESP_FAIL;
    }

    ESP_LOGI(tag, "Auto-detect started (callback: %s)", callback ? "YES" : "NO");
    return ESP_OK;
}

esp_err_t stopAutoDetect() {
    if (!gAutoDetectRunning) {
        return ESP_OK;
    }

    gAutoDetectRunning = false;
    gDetectCallback = nullptr;

    // Wait for task to clean up (max 3 seconds)
    for (int i = 0; i < AutoDetectConfig::CLEANUP_MAX_RETRIES && gAutoDetectTask != nullptr; i++) {
        vTaskDelay(pdMS_TO_TICKS(AutoDetectConfig::CLEANUP_RETRY_MS));
    }

    ESP_LOGI(tag, "Auto-detect stopped");
    return ESP_OK;
}

bool isAutoDetectRunning() {
    return gAutoDetectRunning;
}

// ============================================================================
// HELPER: Set IO Expander Handle
// ============================================================================

/**
 * @brief Set IO expander handle (called by board_drivers during init)
 * @param ioExpander IO expander handle
 */
void setIOExpanderHandle(esp_io_expander_handle_t ioExpander) {
    gIoExpander = ioExpander;
    ESP_LOGI(tag, "IO Expander handle set");
}

}  // namespace sd_services
