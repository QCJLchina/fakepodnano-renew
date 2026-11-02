/**
 * FAKE_POD_NANO 专辑封面服务实现
 */

#include "album_art.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "tjpgd.h"

static const char *TAG = "ART";

static uint8_t *s_jpeg = NULL;          /* 从文件里提取出的 JPEG 数据 */
static size_t s_jpeg_len = 0;
static lv_color_t *s_pixels = NULL;     /* 解码后的像素（PSRAM） */
static lv_img_dsc_t s_dsc;              /* 返回给 LVGL 的描述符 */
static char s_path[256] = {0};
static bool s_valid = false;

/* ---------------------------------------------------------------
 * 缓冲管理
 * --------------------------------------------------------------- */
static void free_jpeg(void)
{
    if (s_jpeg) { free(s_jpeg); s_jpeg = NULL; }
    s_jpeg_len = 0;
}

void album_art_release(void)
{
    free_jpeg();
    if (s_pixels) { heap_caps_free(s_pixels); s_pixels = NULL; }
    s_path[0] = 0;
    s_valid = false;
}

/* ---------------------------------------------------------------
 * FLAC：遍历 metadata block，type==6 为 PICTURE
 * --------------------------------------------------------------- */
static bool extract_flac(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;

    char magic[4];
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "fLaC", 4) != 0) {
        fclose(f);
        return false;
    }

    bool ok = false;
    for (int i = 0; i < 64 && !ok; i++) {
        uint8_t h[4];
        if (fread(h, 1, 4, f) != 4) break;
        bool last = (h[0] & 0x80) != 0;
        int type = h[0] & 0x7F;
        uint32_t len = ((uint32_t)h[1] << 16) | ((uint32_t)h[2] << 8) | h[3];
        if (len > (4u << 20)) break;

        if (type == 6 && len > 32) {
            uint8_t *blk = (uint8_t *)malloc(len);
            if (!blk) break;
            if (fread(blk, 1, len, f) == len) {
                uint32_t mime_len = ((uint32_t)blk[4] << 24) | ((uint32_t)blk[5] << 16) |
                                    ((uint32_t)blk[6] << 8) | blk[7];
                uint32_t off = 8 + mime_len;
                if (off + 4 <= len) {
                    uint32_t desc_len = ((uint32_t)blk[off] << 24) | ((uint32_t)blk[off + 1] << 16) |
                                        ((uint32_t)blk[off + 2] << 8) | blk[off + 3];
                    off += 4 + desc_len;
                    off += 16;  /* width/height/depth/colors */
                    if (off + 4 <= len) {
                        uint32_t data_len = ((uint32_t)blk[off] << 24) | ((uint32_t)blk[off + 1] << 16) |
                                            ((uint32_t)blk[off + 2] << 8) | blk[off + 3];
                        off += 4;
                        if (off + data_len <= len && data_len > 128 &&
                            blk[off] == 0xFF && blk[off + 1] == 0xD8) {
                            s_jpeg = (uint8_t *)malloc(data_len);
                            if (s_jpeg) {
                                memcpy(s_jpeg, blk + off, data_len);
                                s_jpeg_len = data_len;
                                ok = true;
                            }
                        }
                    }
                }
            }
            free(blk);
        } else {
            if (fseek(f, (long)len, SEEK_CUR) != 0) break;
        }
        if (last) break;
    }

    fclose(f);
    return ok;
}

/* ---------------------------------------------------------------
 * ID3v2：找 APIC 帧
 * --------------------------------------------------------------- */
static uint32_t id3_syncsafe(const uint8_t *b)
{
    return ((uint32_t)(b[0] & 0x7F) << 21) | ((uint32_t)(b[1] & 0x7F) << 14) |
           ((uint32_t)(b[2] & 0x7F) << 7) | (b[3] & 0x7F);
}

static bool extract_id3(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;

    uint8_t hdr[10];
    if (fread(hdr, 1, 10, f) != 10 || memcmp(hdr, "ID3", 3) != 0) {
        fclose(f);
        return false;
    }
    uint8_t ver = hdr[3];
    uint32_t size = id3_syncsafe(hdr + 6);
    if (size == 0 || size > (4u << 20)) {
        fclose(f);
        return false;
    }
    uint8_t *tag = (uint8_t *)malloc(size);
    if (!tag) { fclose(f); return false; }
    size_t rd = fread(tag, 1, size, f);
    fclose(f);

    bool ok = false;
    uint32_t off = 0;
    if (hdr[5] & 0x40) {  /* 扩展头 */
        if (size >= 4) {
            uint32_t esz = (ver >= 4) ? id3_syncsafe(tag)
                                      : (((uint32_t)tag[0] << 24) | ((uint32_t)tag[1] << 16) |
                                         ((uint32_t)tag[2] << 8) | tag[3]);
            off = (ver >= 4) ? esz : esz + 4;
        }
    }

    while (off + 10 <= rd) {
        if (tag[off] == 0) break;
        char id[5] = { (char)tag[off], (char)tag[off + 1], (char)tag[off + 2], (char)tag[off + 3], 0 };
        if (!isalnum((unsigned char)id[0])) break;
        uint32_t fsz = (ver >= 4) ? id3_syncsafe(tag + off + 4)
                                  : (((uint32_t)tag[off + 4] << 24) | ((uint32_t)tag[off + 5] << 16) |
                                     ((uint32_t)tag[off + 6] << 8) | tag[off + 7]);
        uint32_t fpos = off + 10;
        if (fsz == 0 || fpos + fsz > rd || fpos + fsz < fpos) break;

        if (!strcmp(id, "APIC") && fsz > 10) {
            uint32_t p = fpos + 1;          /* 跳过 encoding */
            uint8_t enc = tag[fpos];
            /* mime：ASCII，0 结尾 */
            while (p < fpos + fsz && tag[p]) p++;
            p++;                             /* 跳过 0 */
            p++;                             /* 跳过 picture type */
            if (enc == 1 || enc == 2) {      /* UTF-16：双字节 0 结尾 */
                while (p + 1 < fpos + fsz && !(tag[p] == 0 && tag[p + 1] == 0)) p += 2;
                p += 2;
            } else {
                while (p < fpos + fsz && tag[p]) p++;
                p++;
            }
            if (p + 2 <= fpos + fsz && tag[p] == 0xFF && tag[p + 1] == 0xD8) {
                uint32_t dlen = fpos + fsz - p;
                s_jpeg = (uint8_t *)malloc(dlen);
                if (s_jpeg) {
                    memcpy(s_jpeg, tag + p, dlen);
                    s_jpeg_len = dlen;
                    ok = true;
                }
            }
            break;
        }
        off = fpos + fsz;
    }

    free(tag);
    return ok;
}

/* ---------------------------------------------------------------
 * tjpgd 回调
 * --------------------------------------------------------------- */
typedef struct {
    const uint8_t *buf;
    size_t len;
    size_t pos;
    uint16_t w, h;
    lv_color_t *out;
} jctx_t;

static size_t in_func(JDEC *jd, uint8_t *buff, size_t n)
{
    jctx_t *c = (jctx_t *)jd->device;
    size_t avail = (c->pos < c->len) ? (c->len - c->pos) : 0;
    if (n > avail) n = avail;
    if (buff && n) memcpy(buff, c->buf + c->pos, n);
    c->pos += n;
    return n;
}

static int out_func(JDEC *jd, void *bitmap, JRECT *rect)
{
    jctx_t *c = (jctx_t *)jd->device;
    const uint8_t *src = (const uint8_t *)bitmap;
    for (int y = rect->top; y <= rect->bottom; y++) {
        for (int x = rect->left; x <= rect->right; x++) {
            uint8_t r = *src++, g = *src++, b = *src++;
            if ((uint16_t)x < c->w && (uint16_t)y < c->h) {
                c->out[(uint32_t)y * c->w + x] = lv_color_make(r, g, b);
            }
        }
    }
    return 1;
}

/* ---------------------------------------------------------------
 * 对外接口
 * --------------------------------------------------------------- */
bool album_art_get(const char *path, lv_img_dsc_t *out_dsc)
{
    if (!path || !out_dsc) return false;

    if (s_valid && strcmp(path, s_path) == 0) {
        *out_dsc = s_dsc;
        return true;
    }

    album_art_release();

    bool got = extract_flac(path);
    if (!got) got = extract_id3(path);
    if (!got) return false;

    size_t pool_sz = 512 + 4096;
    uint8_t *pool = (uint8_t *)malloc(pool_sz);
    if (!pool) { free_jpeg(); return false; }

    JDEC jd;
    jctx_t ctx = { s_jpeg, s_jpeg_len, 0, 0, 0, NULL };
    JRESULT res = jd_prepare(&jd, in_func, pool, pool_sz, &ctx);
    if (res != JDR_OK) {
        ESP_LOGW(TAG, "jd_prepare 失败 (%d)", (int)res);
        free(pool);
        free_jpeg();
        return false;
    }

    int scale = 0;
    uint32_t maxdim = jd.width > jd.height ? jd.width : jd.height;
    while (scale < 3 && (maxdim >> (scale + 1)) >= 168) scale++;

    uint32_t w = ((uint32_t)jd.width + (1u << scale) - 1) >> scale;
    uint32_t h = ((uint32_t)jd.height + (1u << scale) - 1) >> scale;
    if (w == 0 || h == 0 || (uint64_t)w * h * sizeof(lv_color_t) > (2u << 20)) {
        free(pool);
        free_jpeg();
        return false;
    }

    s_pixels = (lv_color_t *)heap_caps_malloc((size_t)w * h * sizeof(lv_color_t),
                                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_pixels) s_pixels = (lv_color_t *)malloc((size_t)w * h * sizeof(lv_color_t));
    if (!s_pixels) { free(pool); free_jpeg(); return false; }

    ctx.w = (uint16_t)w;
    ctx.h = (uint16_t)h;
    ctx.out = s_pixels;
    ctx.pos = 0;

    res = jd_decomp(&jd, out_func, (uint8_t)scale);
    free(pool);
    if (res != JDR_OK) {
        ESP_LOGW(TAG, "jd_decomp 失败 (%d)", (int)res);
        album_art_release();
        return false;
    }

    memset(&s_dsc, 0, sizeof(s_dsc));
    s_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    s_dsc.header.w = (uint16_t)w;
    s_dsc.header.h = (uint16_t)h;
    s_dsc.data_size = (uint32_t)(w * h * sizeof(lv_color_t));
    s_dsc.data = (const uint8_t *)s_pixels;

    snprintf(s_path, sizeof(s_path), "%s", path);
    s_valid = true;
    *out_dsc = s_dsc;

    free_jpeg();   /* JPEG 数据用完即可释放，像素保留 */

    ESP_LOGI(TAG, "封面解码成功 %dx%d -> %ux%u", jd.width, jd.height, w, h);
    return true;
}
