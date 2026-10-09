#pragma once

// StoxBox SD / screenshot helper
// ESPHome + LVGL 9.x
// Guition ESP32-S3-4848S040
//
// SD card wiring:
//   CLK  GPIO48
//   MOSI GPIO47
//   MISO GPIO41
//   CS   GPIO42
//
// SD is mounted at:
//   /sd
//
// LVGL FATFS drive:
//   S:
//
// Expected runtime assets:
//   /sd/logo.png
//   /sd/back.png
//
// Screenshot:
//   /sd/screen.bmp
//
// IMPORTANT:
// LVGL 9 snapshot support must be enabled with:
//   CONFIG_LV_USE_SNAPSHOT: "1"

#include <lvgl.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <driver/spi_master.h>
#include <driver/sdspi_host.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sys/stat.h>
#include <ff.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cerrno>

namespace stoxbox_sd_detail {

// -----------------------------------------------------------------------------
// Hardware
// -----------------------------------------------------------------------------

static constexpr int SD_CLK  = 48;
static constexpr int SD_MOSI = 47;
static constexpr int SD_MISO = 41;
static constexpr int SD_CS   = 42;

static constexpr const char *MOUNT_POINT = "/sd";
//static constexpr const char *SCREENSHOT_FILE = "/sd/screen.bmp";

static constexpr uint32_t SCREEN_WIDTH  = 480;
static constexpr uint32_t SCREEN_HEIGHT = 480;

// -----------------------------------------------------------------------------
// State
// -----------------------------------------------------------------------------

inline bool mounted = false;
inline bool bus_initialized = false;
inline sdmmc_card_t *card = nullptr;

// -----------------------------------------------------------------------------
// Forward declarations
// -----------------------------------------------------------------------------

inline bool mount_card();

// -----------------------------------------------------------------------------
// File helpers
// -----------------------------------------------------------------------------

inline bool file_exists(const char *path) {
//  struct stat st {};
//  return ::stat(path, &st) == 0 && S_ISREG(st.st_mode);
  FILE *f = fopen(path, "rb");
  if (f != nullptr) {
    fclose(f);
    return true;
  }
  return false;
}

// -----------------------------------------------------------------------------
// LVGL filesystem driver: S: -> /sd
// -----------------------------------------------------------------------------

static bool lvgl_sd_ready(lv_fs_drv_t *drv)
{
  (void)drv;
  return mount_card();
}

static void *lvgl_sd_open(
    lv_fs_drv_t *drv,
    const char *path,
    lv_fs_mode_t mode)
{
  (void)drv;

  if (!mount_card() || path == nullptr) {
    return nullptr;
  }

  char full_path[256];

  if (path[0] == '/') {
    snprintf(full_path, sizeof(full_path), "/sd%s", path);
  } else {
    snprintf(full_path, sizeof(full_path), "/sd/%s", path);
  }

  const char *mode_str = nullptr;

  if (mode == LV_FS_MODE_WR) {
    mode_str = "wb";
  } else {
    mode_str = "rb";
  }

  FILE *f = fopen(full_path, mode_str);

  if (f == nullptr) {
    ESP_LOGW(
        "stoxbox_sd",
        "LVGL FS: fopen failed: %s",
        full_path
    );
    return nullptr;
  }

  ESP_LOGD(
      "stoxbox_sd",
      "LVGL FS open: %s",
      full_path
  );

  return static_cast<void *>(f);
}

static lv_fs_res_t lvgl_sd_close(
    lv_fs_drv_t *drv,
    void *file_p)
{
  (void)drv;

  if (file_p == nullptr) {
    return LV_FS_RES_INV_PARAM;
  }

  FILE *f = static_cast<FILE *>(file_p);

  if (fclose(f) == 0) {
    return LV_FS_RES_OK;
  }

  return LV_FS_RES_FS_ERR;
}

static lv_fs_res_t lvgl_sd_read(
    lv_fs_drv_t *drv,
    void *file_p,
    void *buf,
    uint32_t btr,
    uint32_t *br)
{
  (void)drv;

  if (file_p == nullptr || buf == nullptr || br == nullptr) {
    return LV_FS_RES_INV_PARAM;
  }

  FILE *f = static_cast<FILE *>(file_p);

  size_t n = fread(buf, 1, btr, f);

  *br = static_cast<uint32_t>(n);

  if (ferror(f)) {
    clearerr(f);
    return LV_FS_RES_FS_ERR;
  }

  return LV_FS_RES_OK;
}

static lv_fs_res_t lvgl_sd_write(
    lv_fs_drv_t *drv,
    void *file_p,
    const void *buf,
    uint32_t btw,
    uint32_t *bw)
{
  (void)drv;

  if (file_p == nullptr || buf == nullptr || bw == nullptr) {
    return LV_FS_RES_INV_PARAM;
  }

  FILE *f = static_cast<FILE *>(file_p);

  size_t n = fwrite(buf, 1, btw, f);

  *bw = static_cast<uint32_t>(n);

  if (n != btw) {
    return LV_FS_RES_FS_ERR;
  }

  return LV_FS_RES_OK;
}

static lv_fs_res_t lvgl_sd_seek(
    lv_fs_drv_t *drv,
    void *file_p,
    uint32_t pos,
    lv_fs_whence_t whence)
{
  (void)drv;

  if (file_p == nullptr) {
    return LV_FS_RES_INV_PARAM;
  }

  FILE *f = static_cast<FILE *>(file_p);

  int origin;

  switch (whence) {
    case LV_FS_SEEK_SET:
      origin = SEEK_SET;
      break;

    case LV_FS_SEEK_CUR:
      origin = SEEK_CUR;
      break;

    case LV_FS_SEEK_END:
      origin = SEEK_END;
      break;

    default:
      return LV_FS_RES_INV_PARAM;
  }

  if (fseek(f, static_cast<long>(pos), origin) != 0) {
    return LV_FS_RES_FS_ERR;
  }

  return LV_FS_RES_OK;
}

static lv_fs_res_t lvgl_sd_tell(
    lv_fs_drv_t *drv,
    void *file_p,
    uint32_t *pos_p)
{
  (void)drv;

  if (file_p == nullptr || pos_p == nullptr) {
    return LV_FS_RES_INV_PARAM;
  }

  FILE *f = static_cast<FILE *>(file_p);

  long pos = ftell(f);

  if (pos < 0) {
    return LV_FS_RES_FS_ERR;
  }

  *pos_p = static_cast<uint32_t>(pos);

  return LV_FS_RES_OK;
}

static bool lvgl_sd_fs_registered = false;

inline void register_lvgl_sd_fs()
{
  if (lvgl_sd_fs_registered) {
    return;
  }

  static lv_fs_drv_t drv;

  lv_fs_drv_init(&drv);

  drv.letter = 'S';
  drv.cache_size = 0;

  drv.ready_cb = lvgl_sd_ready;

  drv.open_cb = lvgl_sd_open;
  drv.close_cb = lvgl_sd_close;
  drv.read_cb = lvgl_sd_read;
  drv.write_cb = lvgl_sd_write;
  drv.seek_cb = lvgl_sd_seek;
  drv.tell_cb = lvgl_sd_tell;

  // Directory operations are not needed for loading PNG files.
  drv.dir_open_cb = nullptr;
  drv.dir_read_cb = nullptr;
  drv.dir_close_cb = nullptr;

  drv.user_data = nullptr;

  lv_fs_drv_register(&drv);

  lvgl_sd_fs_registered = true;

  ESP_LOGI(
      "stoxbox_sd",
      "LVGL SD filesystem registered as S:"
  );
}


// -----------------------------------------------------------------------------
// SD card mount
// -----------------------------------------------------------------------------

inline bool mount_card() {
  if (mounted) {
    return true;
  }

  if (!bus_initialized) {
    spi_bus_config_t bus_cfg {};

    bus_cfg.mosi_io_num = SD_MOSI;
    bus_cfg.miso_io_num = SD_MISO;
    bus_cfg.sclk_io_num = SD_CLK;

    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;

    bus_cfg.max_transfer_sz = 64 * 1024;

    esp_err_t err =
        spi_bus_initialize(
            SPI2_HOST,
            &bus_cfg,
            SPI_DMA_CH_AUTO
        );

    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
      return false;
    }

    bus_initialized = true;
  }

  sdmmc_host_t host = SDSPI_HOST_DEFAULT();

  // Conservative speed for maximum SD compatibility.
  host.max_freq_khz = 20000;

  sdspi_device_config_t slot_config =
      SDSPI_DEVICE_CONFIG_DEFAULT();

  slot_config.host_id = SPI2_HOST;
  slot_config.gpio_cs =
      static_cast<gpio_num_t>(SD_CS);

  slot_config.gpio_cd = SDSPI_SLOT_NO_CD;
  slot_config.gpio_wp = SDSPI_SLOT_NO_WP;
  slot_config.gpio_int = SDSPI_SLOT_NO_INT;

  esp_vfs_fat_mount_config_t mount_config {};

  mount_config.format_if_mount_failed = false;
  mount_config.max_files = 8;
  mount_config.allocation_unit_size = 16 * 1024;

  esp_err_t err =
      esp_vfs_fat_sdspi_mount(
          MOUNT_POINT,
          &host,
          &slot_config,
          &mount_config,
          &card
      );

  if (err != ESP_OK) {
    mounted = false;
    card = nullptr;
    return false;
  }

  mounted = true;
  return true;
}

// -----------------------------------------------------------------------------
// BMP helpers
// -----------------------------------------------------------------------------

inline void write_u16(FILE *f, uint16_t value) {
  uint8_t b[2] = {
      static_cast<uint8_t>(value & 0xFF),
      static_cast<uint8_t>((value >> 8) & 0xFF)
  };

  fwrite(b, 1, sizeof(b), f);
}

inline void write_u32(FILE *f, uint32_t value) {
  uint8_t b[4] = {
      static_cast<uint8_t>(value & 0xFF),
      static_cast<uint8_t>((value >> 8) & 0xFF),
      static_cast<uint8_t>((value >> 16) & 0xFF),
      static_cast<uint8_t>((value >> 24) & 0xFF)
  };

  fwrite(b, 1, sizeof(b), f);
}

inline void write_i32(FILE *f, int32_t value) {
  write_u32(f, static_cast<uint32_t>(value));
}

// -----------------------------------------------------------------------------
// RGB565 -> 24 bit RGB
// -----------------------------------------------------------------------------

inline void rgb565_to_rgb888(
    uint16_t c,
    uint8_t &r,
    uint8_t &g,
    uint8_t &b
) {
  r = static_cast<uint8_t>(
      ((c >> 11) & 0x1F) * 255U / 31U
  );

  g = static_cast<uint8_t>(
      ((c >> 5) & 0x3F) * 255U / 63U
  );

  b = static_cast<uint8_t>(
      (c & 0x1F) * 255U / 31U
  );
}

// -----------------------------------------------------------------------------
// Screenshot
// -----------------------------------------------------------------------------
//
// Creates:
//
//   /sd/SCR_xxxx.BMP
//
// Format:
//
//   480 x 480
//   24 bit
//   uncompressed BMP
//   top-down
//   BGR pixel order
//
// `rotation` is intentionally NOT applied here.
//
// LVGL's snapshot represents the current LVGL screen. Applying rotation
// again here could rotate the screenshot twice.
// -----------------------------------------------------------------------------

inline bool save_snapshot_bmp(int rotation) {
#if !LV_USE_SNAPSHOT

  ESP_LOGW("stoxbox_sd", "LV_USE_SNAPSHOT is disabled");
  return false;

#else

  // --------------------------------------------------------------------------
  // Mount SD card
  // --------------------------------------------------------------------------

  if (!mount_card()) {
    ESP_LOGE("stoxbox_sd", "SD card is not mounted");
    return false;
  }

  // --------------------------------------------------------------------------
  // Take LVGL snapshot
  // --------------------------------------------------------------------------

  lv_obj_t *screen = lv_screen_active();

  lv_draw_buf_t *snapshot =
      lv_snapshot_take(screen, LV_COLOR_FORMAT_RGB565);

  if (snapshot == nullptr) {
    ESP_LOGE("stoxbox_sd", "lv_snapshot_take() failed");
    return false;
  }

  const int src_w = snapshot->header.w;
  const int src_h = snapshot->header.h;
  const int src_stride = snapshot->header.stride;

  ESP_LOGI("stoxbox_sd",
           "Snapshot: %d x %d, stride=%d",
           src_w,
           src_h,
           src_stride);

  if (src_w != 480 || src_h != 480) {
    ESP_LOGE("stoxbox_sd",
             "Unexpected snapshot size: %d x %d",
             src_w,
             src_h);

    lv_draw_buf_destroy(snapshot);
    return false;
  }

  // --------------------------------------------------------------------------
  // Find next free filename
  //
  //   scr_0001.bmp
  //   scr_0002.bmp
  //   ...
  //   scr_9999.bmp
  // --------------------------------------------------------------------------

  char filename[64];
  int file_number = -1;

  for (int i = 1; i <= 9999; i++) {

    snprintf(
        filename,
        sizeof(filename),
        "/sd/SCR_%04d.BMP",
        i);

    if (!file_exists(filename)) {
      file_number = i;
      break;
    }
  }

  if (file_number < 0) {

    ESP_LOGE(
        "stoxbox_sd",
        "No free screenshot filename "
        "(SCR_0001.BMP ... SCR_9999.BMP)");

    lv_draw_buf_destroy(snapshot);
    return false;
  }

  ESP_LOGI(
      "stoxbox_sd",
      "Saving screenshot to %s",
      filename);

  // --------------------------------------------------------------------------
  // Open file
  // --------------------------------------------------------------------------

  FILE *f = fopen(filename, "wb");

  if (f == nullptr) {

    ESP_LOGE(
        "stoxbox_sd",
        "fopen(%s) failed, errno=%d (%s)",
        filename,
        errno,
        strerror(errno));

    lv_draw_buf_destroy(snapshot);
    return false;
  }

  // --------------------------------------------------------------------------
  // IMPORTANT:
  //
  // Use a 16 KB stdio buffer so that the many 1440-byte fwrite() calls
  // do not necessarily become individual FATFS/SD writes.
  //
  // Static is intentional:
  // the buffer must remain valid until fclose().
  // --------------------------------------------------------------------------

  static uint8_t file_buffer[16 * 1024];

  if (setvbuf(
          f,
          reinterpret_cast<char *>(file_buffer),
          _IOFBF,
          sizeof(file_buffer)) != 0) {

    ESP_LOGW(
        "stoxbox_sd",
        "setvbuf() failed, continuing without custom buffer");
  }

  // --------------------------------------------------------------------------
  // BMP parameters
  //
  // 480 x 480
  // 24-bit BGR
  //
  // 480 * 3 = 1440 bytes / row
  // 1440 is divisible by 4, therefore no row padding is needed.
  // --------------------------------------------------------------------------

  const uint32_t out_w = 480;
  const uint32_t out_h = 480;

  const uint32_t bytes_per_pixel = 3;

  const uint32_t row_bytes =
      out_w * bytes_per_pixel;

  const uint32_t image_bytes =
      row_bytes * out_h;

  const uint32_t file_header_size = 14;
  const uint32_t info_header_size = 40;

  const uint32_t pixel_offset =
      file_header_size + info_header_size;

  const uint32_t file_size =
      pixel_offset + image_bytes;

  // --------------------------------------------------------------------------
  // BMP FILE HEADER
  // --------------------------------------------------------------------------

  if (fwrite("BM", 1, 2, f) != 2) {

    ESP_LOGE(
        "stoxbox_sd",
        "Failed to write BMP signature");

    fclose(f);
    lv_draw_buf_destroy(snapshot);
    remove(filename);

    return false;
  }

  write_u32(f, file_size);
  write_u16(f, 0);
  write_u16(f, 0);
  write_u32(f, pixel_offset);

  // --------------------------------------------------------------------------
  // BMP INFO HEADER
  // --------------------------------------------------------------------------

  write_u32(f, info_header_size);

  write_i32(
      f,
      static_cast<int32_t>(out_w));

  // Negative height = top-down BMP.
  write_i32(
      f,
      -static_cast<int32_t>(out_h));

  write_u16(f, 1);    // planes
  write_u16(f, 24);   // bits per pixel
  write_u32(f, 0);    // BI_RGB
  write_u32(f, image_bytes);

  // 72 DPI
  write_i32(f, 2835);
  write_i32(f, 2835);

  write_u32(f, 0);
  write_u32(f, 0);

  // --------------------------------------------------------------------------
  // Allocate one complete output row.
  // --------------------------------------------------------------------------

  uint8_t *row_buffer =
      static_cast<uint8_t *>(
          heap_caps_malloc(
              row_bytes,
              MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));

  if (row_buffer == nullptr) {

    ESP_LOGE(
        "stoxbox_sd",
        "Could not allocate %lu byte row buffer",
        static_cast<unsigned long>(row_bytes));

    fclose(f);
    lv_draw_buf_destroy(snapshot);
    remove(filename);

    return false;
  }

  const uint8_t *src =
      static_cast<const uint8_t *>(snapshot->data);

  const uint16_t *src_pixels =
      reinterpret_cast<const uint16_t *>(src);

  const int src_stride_pixels =
      src_stride / 2;

  // --------------------------------------------------------------------------
  // Write image
  // --------------------------------------------------------------------------

  bool write_ok = true;

  for (int y = 0; y < 480; y++) {

    // ------------------------------------------------------------------------
    // Convert one output row.
    // ------------------------------------------------------------------------

    for (int x = 0; x < 480; x++) {

      int sx = x;
      int sy = y;

      switch (rotation) {

        case 90:
          sx = y;
          sy = 479 - x;
          break;

        case 180:
          sx = 479 - x;
          sy = 479 - y;
          break;

        case 270:
          sx = 479 - y;
          sy = x;
          break;

        case 0:
        default:
          sx = x;
          sy = y;
          break;
      }

      const uint16_t pixel =
          src_pixels[
              sy * src_stride_pixels + sx];

      // RGB565 -> RGB888

      const uint8_t r5 =
          (pixel >> 11) & 0x1F;

      const uint8_t g6 =
          (pixel >> 5) & 0x3F;

      const uint8_t b5 =
          pixel & 0x1F;

      const uint8_t r =
          static_cast<uint8_t>(
              (r5 * 255 + 15) / 31);

      const uint8_t g =
          static_cast<uint8_t>(
              (g6 * 255 + 31) / 63);

      const uint8_t b =
          static_cast<uint8_t>(
              (b5 * 255 + 15) / 31);

      // BMP = BGR

      row_buffer[x * 3 + 0] = b;
      row_buffer[x * 3 + 1] = g;
      row_buffer[x * 3 + 2] = r;
    }

    // ------------------------------------------------------------------------
    // Write complete row with ONE fwrite().
    // ------------------------------------------------------------------------

    const size_t written =
        fwrite(
            row_buffer,
            1,
            row_bytes,
            f);

    if (written != row_bytes) {

      ESP_LOGE(
          "stoxbox_sd",
          "fwrite failed at row %d: wrote %lu / %lu bytes, errno=%d (%s)",
          y,
          static_cast<unsigned long>(written),
          static_cast<unsigned long>(row_bytes),
          errno,
          strerror(errno));

      write_ok = false;
      break;
    }

    // ------------------------------------------------------------------------
    // Give FreeRTOS / watchdog time to run.
    //
    // Every 8 rows:
    // 8 * 1440 = 11520 bytes of image data.
    // ------------------------------------------------------------------------

    if ((y & 0x07) == 0x07) {
      vTaskDelay(pdMS_TO_TICKS(1));
    }

    // ------------------------------------------------------------------------
    // Only occasional progress logging.
    // ------------------------------------------------------------------------

    /*if ((y % 64) == 0) {

      ESP_LOGI(
          "stoxbox_sd",
          "Screenshot: row %d / 480",
          y);
    }*/
  }

  // --------------------------------------------------------------------------
  // Free row buffer
  // --------------------------------------------------------------------------

  heap_caps_free(row_buffer);

  // --------------------------------------------------------------------------
  // Handle write error
  // --------------------------------------------------------------------------

  if (!write_ok) {

    fclose(f);

    lv_draw_buf_destroy(snapshot);

    remove(filename);

    ESP_LOGE(
        "stoxbox_sd",
        "Incomplete screenshot removed: %s",
        filename);

    return false;
  }

  // --------------------------------------------------------------------------
  // Flush stdio buffer
  // --------------------------------------------------------------------------

  if (fflush(f) != 0) {

    ESP_LOGE(
        "stoxbox_sd",
        "fflush(%s) failed, errno=%d (%s)",
        filename,
        errno,
        strerror(errno));

    fclose(f);

    lv_draw_buf_destroy(snapshot);

    remove(filename);

    return false;
  }

  // --------------------------------------------------------------------------
  // Get final file size before fclose().
  // --------------------------------------------------------------------------

  long final_size = ftell(f);

  if (final_size < 0) {

    ESP_LOGW(
        "stoxbox_sd",
        "ftell() failed for %s",
        filename);
  }

  // --------------------------------------------------------------------------
  // Close file
  // --------------------------------------------------------------------------

  fclose(f);

  // --------------------------------------------------------------------------
  // Destroy LVGL snapshot
  // --------------------------------------------------------------------------

  lv_draw_buf_destroy(snapshot);

  // --------------------------------------------------------------------------
  // Done
  // --------------------------------------------------------------------------

  ESP_LOGI(
      "stoxbox_sd",
      "Screenshot saved: %s, %ld bytes",
      filename,
      final_size);

  return true;

#endif
}

// -----------------------------------------------------------------------------
// Runtime logo/background assets
// -----------------------------------------------------------------------------
//
// Runtime files on SD:
//
//   /sd/BACK.BMP
//   /sd/LOGO.BMP
//
// Expected format:
//
//   uncompressed BMP
//   16-bit RGB565
//
// The BMP is read ONCE from SD and copied into PSRAM.
//
// After that:
//   - no SD access
//   - no LVGL filesystem access
//   - no BMP decoder
//   - no repeated image loading
//
// LVGL receives an lv_image_dsc_t which points directly to the PSRAM buffer.
//
// BACK.BMP is normally 480 x 480.
// LOGO.BMP may be smaller.
//
// BMP may be:
//   - bottom-up (normal BMP)
//   - top-down (negative height)
//
// Only uncompressed 16-bit RGB565 BMP is accepted here.
// This keeps runtime loading extremely simple and fast.
// -----------------------------------------------------------------------------

struct RuntimeBmpCache {
  lv_image_dsc_t image{};

  uint8_t *data = nullptr;
  size_t data_size = 0;

  uint16_t width = 0;
  uint16_t height = 0;
  uint32_t stride = 0;

  bool loaded = false;

  char filename[32] = {0};
};


// -----------------------------------------------------------------------------
// Read little-endian values
// -----------------------------------------------------------------------------

inline uint16_t read_u16(FILE *f, bool &ok)
{
  uint8_t b[2];

  if (fread(b, 1, 2, f) != 2) {
    ok = false;
    return 0;
  }

  return
      static_cast<uint16_t>(b[0]) |
      (static_cast<uint16_t>(b[1]) << 8);
}


inline uint32_t read_u32(FILE *f, bool &ok)
{
  uint8_t b[4];

  if (fread(b, 1, 4, f) != 4) {
    ok = false;
    return 0;
  }

  return
      static_cast<uint32_t>(b[0]) |
      (static_cast<uint32_t>(b[1]) << 8) |
      (static_cast<uint32_t>(b[2]) << 16) |
      (static_cast<uint32_t>(b[3]) << 24);
}


inline int32_t read_i32(FILE *f, bool &ok)
{
  return static_cast<int32_t>(
      read_u32(f, ok)
  );
}


// -----------------------------------------------------------------------------
// Release cached image
// -----------------------------------------------------------------------------

inline void free_runtime_bmp(RuntimeBmpCache &cache)
{
  if (cache.data != nullptr) {
    heap_caps_free(cache.data);
    cache.data = nullptr;
  }

  cache.data_size = 0;
  cache.width = 0;
  cache.height = 0;
  cache.stride = 0;
  cache.loaded = false;

  cache.filename[0] = '\0';

  memset(
      &cache.image,
      0,
      sizeof(cache.image)
  );
}


// -----------------------------------------------------------------------------
// Load 16-bit RGB565 BMP into PSRAM
// -----------------------------------------------------------------------------

inline bool load_runtime_bmp(
    const char *filename,
    RuntimeBmpCache &cache)
{
  if (filename == nullptr) {
    return false;
  }

  //
  // Already loaded.
  //
  // This is the important part:
  //
  // apply_runtime_assets() can be called any number of times,
  // but after the first successful load there is NO SD access.
  //
  if (cache.loaded &&
      cache.data != nullptr &&
      strcmp(cache.filename, filename) == 0) {

    return true;
  }

  //
  // Make sure SD is mounted.
  //
  if (!mount_card()) {
    ESP_LOGW(
        "stoxbox_sd",
        "Cannot load runtime BMP, SD mount failed: %s",
        filename
    );

    return false;
  }

  //
  // Open BMP.
  //
  FILE *f = fopen(filename, "rb");

  if (f == nullptr) {
    ESP_LOGW(
        "stoxbox_sd",
        "Cannot open runtime BMP: %s, errno=%d (%s)",
        filename,
        errno,
        strerror(errno)
    );

    return false;
  }

  bool ok = true;

  //
  // --------------------------------------------------------------------------
  // BMP FILE HEADER
  // --------------------------------------------------------------------------
  //

  uint16_t signature = read_u16(f, ok);

  if (!ok || signature != 0x4D42) {
    ESP_LOGW(
        "stoxbox_sd",
        "Not a BMP file: %s",
        filename
    );

    fclose(f);
    return false;
  }

  //
  // File size
  //
  (void)read_u32(f, ok);

  //
  // Reserved
  //
  (void)read_u16(f, ok);
  (void)read_u16(f, ok);

  //
  // Pixel data offset
  //
  uint32_t pixel_offset = read_u32(f, ok);

  if (!ok) {
    fclose(f);
    return false;
  }

  //
  // --------------------------------------------------------------------------
  // DIB HEADER
  // --------------------------------------------------------------------------
  //

  uint32_t dib_size = read_u32(f, ok);

  if (!ok || dib_size < 40) {
    ESP_LOGW(
        "stoxbox_sd",
        "Unsupported BMP DIB header: %s",
        filename
    );

    fclose(f);
    return false;
  }

  int32_t bmp_width = read_i32(f, ok);
  int32_t bmp_height = read_i32(f, ok);

  uint16_t planes = read_u16(f, ok);
  uint16_t bits_per_pixel = read_u16(f, ok);

  uint32_t compression = read_u32(f, ok);

  //
  // The remaining standard BITMAPINFOHEADER fields.
  //
  (void)read_u32(f, ok);  // image size
  (void)read_i32(f, ok);  // X pixels/meter
  (void)read_i32(f, ok);  // Y pixels/meter
  (void)read_u32(f, ok);  // colors used
  (void)read_u32(f, ok);  // important colors

  if (!ok) {
    fclose(f);
    return false;
  }

  //
  // Only one plane is valid.
  //
  if (planes != 1) {
    ESP_LOGW(
        "stoxbox_sd",
        "Unsupported BMP planes=%u: %s",
        (unsigned)planes,
        filename
    );

    fclose(f);
    return false;
  }

  //
  // Runtime format is intentionally limited to:
  //
  //   16 bit
  //   RGB565
  //   uncompressed / BI_RGB
  //
  // This gives us direct compatibility with LVGL RGB565.
  //
  if (bits_per_pixel != 16 || compression != 3) {

    ESP_LOGW(
        "stoxbox_sd",
        "BMP must be uncompressed 16-bit RGB565: %s "
        "(bpp=%u compression=%lu)",
        filename,
        (unsigned)bits_per_pixel,
        (unsigned long)compression
    );

    fclose(f);
    return false;
  }

  //
  // Validate dimensions.
  //
  if (bmp_width <= 0 || bmp_height == 0) {
    ESP_LOGW(
        "stoxbox_sd",
        "Invalid BMP dimensions: %s (%ld x %ld)",
        filename,
        (long)bmp_width,
        (long)bmp_height
    );

    fclose(f);
    return false;
  }

  uint32_t width =
      static_cast<uint32_t>(bmp_width);

  uint32_t height =
      static_cast<uint32_t>(
          bmp_height < 0
              ? -bmp_height
              : bmp_height
      );

  //
  // Keep dimensions within LVGL header limits.
  //
  if (width > 65535 || height > 65535) {
    fclose(f);
    return false;
  }

  //
  // BMP row size.
  //
  //
  // 16 bit = 2 bytes/pixel.
  //
  // BMP rows are padded to 4-byte boundaries.
  //
  const uint32_t bmp_row_bytes =
      ((width * 2U + 3U) / 4U) * 4U;

  //
  // LVGL RGB565 image has no padding for normal RGB565.
  //
  const uint32_t lvgl_stride =
      width * 2U;

  const size_t image_size =
      static_cast<size_t>(lvgl_stride) *
      static_cast<size_t>(height);

  //
  // Allocate image data in PSRAM.
  //
  uint8_t *psram_data =
      static_cast<uint8_t *>(
          heap_caps_malloc(
              image_size,
              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
          )
      );

  if (psram_data == nullptr) {

    ESP_LOGE(
        "stoxbox_sd",
        "PSRAM allocation failed for %s: %u bytes",
        filename,
        (unsigned)image_size
    );

    fclose(f);
    return false;
  }

  //
  // Temporary one-row BMP buffer.
  //
  uint8_t *row_buffer =
      static_cast<uint8_t *>(
          heap_caps_malloc(
              bmp_row_bytes,
              MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT
          )
      );

  if (row_buffer == nullptr) {

    ESP_LOGE(
        "stoxbox_sd",
        "Could not allocate BMP row buffer: %lu bytes",
        (unsigned long)bmp_row_bytes
    );

    heap_caps_free(psram_data);
    fclose(f);

    return false;
  }

  //
  // Move to pixel data.
  //
  if (fseek(
          f,
          static_cast<long>(pixel_offset),
          SEEK_SET) != 0) {

    ESP_LOGE(
        "stoxbox_sd",
        "fseek() failed for %s",
        filename
    );

    heap_caps_free(row_buffer);
    heap_caps_free(psram_data);
    fclose(f);

    return false;
  }

  //
  // Positive BMP height:
  //
  //   bottom-up
  //
  // Negative BMP height:
  //
  //   top-down
  //
  const bool bottom_up =
      bmp_height > 0;

  //
  // --------------------------------------------------------------------------
  // Read rows
  // --------------------------------------------------------------------------
  //

  for (uint32_t file_y = 0;
       file_y < height;
       file_y++) {

    if (fread(
            row_buffer,
            1,
            bmp_row_bytes,
            f) != bmp_row_bytes) {

      ESP_LOGE(
          "stoxbox_sd",
          "BMP read failed at row %lu: %s",
          (unsigned long)file_y,
          filename
      );

      ok = false;
      break;
    }

    //
    // Destination row.
    //
    uint32_t dst_y =
        bottom_up
            ? (height - 1U - file_y)
            : file_y;

    uint8_t *dst =
        psram_data +
        static_cast<size_t>(dst_y) *
        lvgl_stride;

    //
    // BMP RGB565 is little-endian.
    //
    // ESP32-S3 is little-endian too, so the bytes can be copied
    // directly into the LVGL RGB565 buffer.
    //
    memcpy(
        dst,
        row_buffer,
        lvgl_stride
    );

    //
    // Give FreeRTOS some breathing room for large images.
    //
    if ((file_y & 0x1F) == 0x1F) {
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }

  heap_caps_free(row_buffer);
  fclose(f);

  //
  // --------------------------------------------------------------------------
  // Failed read
  // --------------------------------------------------------------------------
  //

  if (!ok) {

    heap_caps_free(psram_data);

    return false;
  }

  //
  // --------------------------------------------------------------------------
  // Replace previous cache
  // --------------------------------------------------------------------------
  //

  free_runtime_bmp(cache);

  cache.data = psram_data;
  cache.data_size = image_size;

  cache.width =
      static_cast<uint16_t>(width);

  cache.height =
      static_cast<uint16_t>(height);

  cache.stride =
      lvgl_stride;

  //
  // --------------------------------------------------------------------------
  // LVGL image descriptor
  // --------------------------------------------------------------------------
  //
  // LVGL 9 image descriptor:
  //
  //   RGB565
  //   width
  //   height
  //   stride
  //   PSRAM data
  //
  // The LVGL 9 image descriptor contains magic/cf/flags/w/h/stride.
  // This is the same structure used by generated RGB565 images.
  //
  // LVGL's RGB565 format is directly supported by the renderer.
  // --------------------------------------------------------------------------

  memset(
      &cache.image,
      0,
      sizeof(cache.image)
  );

  cache.image.header.magic =
      LV_IMAGE_HEADER_MAGIC;

  cache.image.header.cf =
      LV_COLOR_FORMAT_RGB565;

  cache.image.header.flags = 0;

  cache.image.header.w =
      static_cast<uint16_t>(width);

  cache.image.header.h =
      static_cast<uint16_t>(height);

  cache.image.header.stride =
      static_cast<uint16_t>(lvgl_stride);

  cache.image.header.reserved_2 = 0;

  cache.image.data_size =
      static_cast<uint32_t>(image_size);

  cache.image.data =
      cache.data;

  cache.image.reserved = nullptr;

  //
  // Remember which file is cached.
  //
  strncpy(
      cache.filename,
      filename,
      sizeof(cache.filename) - 1
  );

  cache.filename[
      sizeof(cache.filename) - 1
  ] = '\0';

  cache.loaded = true;

  ESP_LOGI(
      "stoxbox_sd",
      "Runtime BMP cached in PSRAM: %s "
      "%ux%u, %lu bytes",
      filename,
      (unsigned)cache.width,
      (unsigned)cache.height,
      (unsigned long)cache.data_size
  );

  return true;
}


// -----------------------------------------------------------------------------
// Runtime caches
// -----------------------------------------------------------------------------

inline RuntimeBmpCache g_logo_bmp_cache;
inline RuntimeBmpCache g_back_bmp_cache;


// -----------------------------------------------------------------------------
// Apply one cached image to a widget
// -----------------------------------------------------------------------------

inline void apply_cached_bmp(
    lv_obj_t *widget,
    RuntimeBmpCache &cache)
{
  if (widget == nullptr ||
      !cache.loaded ||
      cache.data == nullptr) {

    return;
  }

  //
  // Avoid calling lv_image_set_src() again if this widget already
  // points to this exact descriptor.
  //
  const void *current =
      lv_image_get_src(widget);

  const void *wanted =
      static_cast<const void *>(&cache.image);

  if (current == wanted) {
    return;
  }

  lv_image_set_src(
      widget,
      &cache.image
  );
}


// -----------------------------------------------------------------------------
// Apply runtime assets
// -----------------------------------------------------------------------------

inline void apply_runtime_assets(
    lv_obj_t *logo_widget,
    lv_obj_t *back1,
    lv_obj_t *back2,
    lv_obj_t *back3)
{
  //
  // --------------------------------------------------------------------------
  // LOGO
  // --------------------------------------------------------------------------
  //

  if (!g_logo_bmp_cache.loaded) {

    if (file_exists("/sd/LOGO.BMP")) {

      load_runtime_bmp(
          "/sd/LOGO.BMP",
          g_logo_bmp_cache
      );
    }
  }

  //
  // --------------------------------------------------------------------------
  // BACKGROUND
  // --------------------------------------------------------------------------
  //

  if (!g_back_bmp_cache.loaded) {

    if (file_exists("/sd/BACK.BMP")) {

      load_runtime_bmp(
          "/sd/BACK.BMP",
          g_back_bmp_cache
      );
    }
  }

  //
  // --------------------------------------------------------------------------
  // Apply from PSRAM.
  //
  // From this point there is NO SD access.
  // --------------------------------------------------------------------------
  //

  apply_cached_bmp(
      logo_widget,
      g_logo_bmp_cache
  );

  apply_cached_bmp(
      back1,
      g_back_bmp_cache
  );

  apply_cached_bmp(
      back2,
      g_back_bmp_cache
  );

  apply_cached_bmp(
      back3,
      g_back_bmp_cache
  );
}

// -----------------------------------------------------------------------------
// Public status helper
// -----------------------------------------------------------------------------

inline bool exists() {
  return mount_card();
}

}  // namespace stoxbox_sd_detail

inline void stoxbox_sd_list_files() {
  if (!stoxbox_sd_detail::mount_card()) {
    ESP_LOGW("stoxbox_sd", "SD card is not mounted");
    return;
  }

  FF_DIR dir;
  FILINFO info;

  FRESULT res = f_opendir(&dir, "0:/");

  if (res != FR_OK) {
    ESP_LOGE(
        "stoxbox_sd",
        "f_opendir failed: %d",
        (int) res
    );
    return;
  }

  ESP_LOGI("stoxbox_sd", "========== SD FILE LIST ==========");

  uint32_t count = 0;

  while (true) {

    res = f_readdir(&dir, &info);

    if (res != FR_OK) {
      ESP_LOGE(
          "stoxbox_sd",
          "f_readdir failed: %d",
          (int) res
      );
      break;
    }

    // End of directory
    if (info.fname[0] == '\0') {
      break;
    }

    if (info.fattrib & AM_DIR) {
      ESP_LOGI(
          "stoxbox_sd",
          "[DIR ] %s",
          info.fname
      );
    } else {
      ESP_LOGI(
          "stoxbox_sd",
          "[FILE] %-32s %lu bytes",
          info.fname,
          (unsigned long) info.fsize
      );
    }

    count++;
  }

  f_closedir(&dir);

  ESP_LOGI(
      "stoxbox_sd",
      "========== %lu entries ==========",
      (unsigned long) count
  );
}

// =============================================================================
// Public functions used from ESPHome YAML
// =============================================================================

inline bool stoxbox_sd_get_info(
    uint32_t &file_count,
    uint64_t &free_bytes,
    uint64_t &total_bytes) {

  file_count = 0;
  free_bytes = 0;
  total_bytes = 0;

  if (!stoxbox_sd_detail::mount_card()) {
//    ESP_LOGW("stoxbox_sd", "SD card is not mounted");
    return false;
  }

  // ------------------------------------------------------------
  // FATFS free / total space
  // ------------------------------------------------------------

  FATFS *fs = nullptr;
  DWORD free_clusters = 0;

  FRESULT res = f_getfree("0:", &free_clusters, &fs);

  if (res != FR_OK || fs == nullptr) {
    ESP_LOGW(
        "stoxbox_sd",
        "f_getfree() failed: %d",
        (int) res
    );
    return false;
  }

  uint32_t sector_size = fs->ssize;

  if (sector_size == 0) {
    sector_size = 512;
  }

  uint64_t total_clusters =
      (uint64_t)(fs->n_fatent - 2);

  total_bytes =
      total_clusters *
      (uint64_t)fs->csize *
      (uint64_t)sector_size;

  free_bytes =
      (uint64_t)free_clusters *
      (uint64_t)fs->csize *
      (uint64_t)sector_size;

  // ------------------------------------------------------------
  // Count files in SD root directory using FATFS directly
  // ------------------------------------------------------------

  FF_DIR dir;
  FILINFO fno;

  memset(&dir, 0, sizeof(dir));
  memset(&fno, 0, sizeof(fno));

  res = f_opendir(&dir, "0:/");

  if (res != FR_OK) {
    ESP_LOGW(
        "stoxbox_sd",
        "f_opendir(0:/) failed: %d",
        (int)res
    );
    return false;
  }

  while (true) {
    res = f_readdir(&dir, &fno);

    if (res != FR_OK) {
      ESP_LOGW(
          "stoxbox_sd",
          "f_readdir() failed: %d",
          (int)res
      );
      f_closedir(&dir);
      return false;
    }

    // End of directory.
    if (fno.fname[0] == '\0') {
      break;
    }

    // Skip directories.
    if ((fno.fattrib & AM_DIR) != 0) {
      continue;
    }

    file_count++;
  }

  f_closedir(&dir);

  ESP_LOGI(
      "stoxbox_sd",
      "SD info: %lu files, %llu/%llu MB free/total",
      (unsigned long)file_count,
      (unsigned long long)
          (free_bytes / (1024ULL * 1024ULL)),
      (unsigned long long)
          (total_bytes / (1024ULL * 1024ULL))
  );

  return true;
}



inline bool stoxbox_sd_mount() {
  return stoxbox_sd_detail::mount_card();
}


inline bool stoxbox_sd_screenshot(int rotation) {
  return stoxbox_sd_detail::save_snapshot_bmp(rotation);
}


inline void stoxbox_sd_apply_assets(
    lv_obj_t *logo_widget,
    lv_obj_t *back1,
    lv_obj_t *back2,
    lv_obj_t *back3
) {
  stoxbox_sd_detail::apply_runtime_assets(
      logo_widget,
      back1,
      back2,
      back3
  );
}


inline bool stoxbox_sd_file_exists(
    const char *path
) {
  return stoxbox_sd_detail::file_exists(path);
}

