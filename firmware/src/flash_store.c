#include <string.h>
#include "flash_store.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/flash.h"

// Last 4 KB sector of flash holds the config blob.
#define FLASH_TARGET_OFFSET   (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define FLASH_MAGIC           0x4A4F5931u   // 'JOY1'

typedef struct __attribute__((packed)) {
    uint32_t      magic;
    joy_config_t  config;
} flash_blob_t;

_Static_assert(sizeof(flash_blob_t) <= FLASH_SECTOR_SIZE, "config blob exceeds flash sector");

static const flash_blob_t *flash_blob(void) {
    return (const flash_blob_t *)(XIP_BASE + FLASH_TARGET_OFFSET);
}

bool flash_store_load(joy_config_t *out) {
    const flash_blob_t *blob = flash_blob();
    if (blob->magic == FLASH_MAGIC &&
        blob->config.version == CONFIG_VERSION &&
        blob->config.crc32 == config_crc32(&blob->config))
    {
        memcpy(out, &blob->config, sizeof(*out));
        return true;
    }
    config_set_defaults(out);
    return false;
}

// flash_range_erase/program must run with interrupts disabled and (on the
// affected core) without the other core executing from XIP. We're single-core,
// so disabling interrupts is sufficient.
typedef struct {
    const uint8_t *page;
} save_ctx_t;

static void do_save(void *param) {
    const save_ctx_t *ctx = (const save_ctx_t *)param;
    flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_TARGET_OFFSET, ctx->page, FLASH_PAGE_SIZE * 2);
}

bool flash_store_save(joy_config_t *c) {
    c->version = CONFIG_VERSION;
    c->crc32   = config_crc32(c);

    // Programming unit is FLASH_PAGE_SIZE (256 B). Pad blob to 2 pages (512 B),
    // which comfortably covers the struct.
    static uint8_t page[FLASH_PAGE_SIZE * 2] __attribute__((aligned(4)));
    memset(page, 0xFF, sizeof(page));
    flash_blob_t blob = { .magic = FLASH_MAGIC, .config = *c };
    _Static_assert(sizeof(blob) <= sizeof(page), "blob larger than program window");
    memcpy(page, &blob, sizeof(blob));

    save_ctx_t ctx = { .page = page };
    int rc = flash_safe_execute(do_save, &ctx, 250);
    return rc == PICO_OK;
}
