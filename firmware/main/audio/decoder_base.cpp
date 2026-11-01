/**
 * FAKE_POD_NANO 音频解码器工厂实现
 */

#include "decoder_base.h"
#include "decoder_wav.h"
#include "decoder_flac.h"
#include "decoder_mp3.h"
#include "decoder_dsd.h"
#include "decoder_aac.h"

#include <string.h>
#include <ctype.h>
#include "esp_log.h"

static const char *TAG = "AUDIO_DEC";

static bool str_ends_with_nocase(const char *str, const char *suffix)
{
    if (!str || !suffix) return false;
    size_t len_str = strlen(str);
    size_t len_suf = strlen(suffix);
    if (len_str < len_suf) return false;

    const char *p_str = str + (len_str - len_suf);
    for (size_t i = 0; i < len_suf; i++) {
        if (tolower((unsigned char)p_str[i]) != tolower((unsigned char)suffix[i])) {
            return false;
        }
    }
    return true;
}

AudioDecoder* audio_decoder_create_for_file(const char *filepath, FILE *f)
{
    if (!filepath || !f) return NULL;

    AudioDecoder *dec = NULL;

    /* 1. 先根据后缀判定 */
    if (str_ends_with_nocase(filepath, ".wav")) {
        dec = new WavDecoder();
    } else if (str_ends_with_nocase(filepath, ".flac")) {
        dec = new FlacDecoder();
    } else if (str_ends_with_nocase(filepath, ".mp3")) {
        dec = new Mp3Decoder();
    } else if (str_ends_with_nocase(filepath, ".dsf") || str_ends_with_nocase(filepath, ".dsd")) {
        dec = new DsdDecoder();
    } else if (str_ends_with_nocase(filepath, ".m4a") || str_ends_with_nocase(filepath, ".aac")) {
        dec = new AacDecoder();
    }

    /* 2. 若根据后缀匹配成功，尝试打开文件解析 */
    if (dec) {
        if (dec->open(f)) {
            return dec;
        }
        delete dec;
        dec = NULL;
    }

    /* 3. 后缀不明确或打开失败时，尝试逐个嗅探魔数 */
    ESP_LOGW(TAG, "根据文件名后缀未成功打开解码器，尝试自动嗅探魔数...");

    dec = new FlacDecoder();
    if (dec->open(f)) return dec;
    delete dec;

    dec = new WavDecoder();
    if (dec->open(f)) return dec;
    delete dec;

    dec = new DsdDecoder();
    if (dec->open(f)) return dec;
    delete dec;

    dec = new Mp3Decoder();
    if (dec->open(f)) return dec;
    delete dec;

    dec = new AacDecoder();
    if (dec->open(f)) return dec;
    delete dec;

    ESP_LOGE(TAG, "不支持的音频格式: %s", filepath);
    return NULL;
}
