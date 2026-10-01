#include "bongo_cat/file.h"
#include "bongo_cat/sha256.h"

#include <stdio.h>
#include <string.h>

typedef struct Sha256 {
    uint32_t state[8];
    uint64_t bits;
    unsigned char block[64];
    size_t used;
} Sha256;

static const uint32_t constants[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};

static uint32_t rotate(uint32_t value, unsigned count) {
    return (value >> count) | (value << (32 - count));
}

static void transform(Sha256 *value) {
    uint32_t words[64];
    for (int i = 0; i < 16; ++i) words[i] =
        ((uint32_t)value->block[i * 4] << 24) | ((uint32_t)value->block[i * 4 + 1] << 16) |
        ((uint32_t)value->block[i * 4 + 2] << 8) | value->block[i * 4 + 3];
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = rotate(words[i - 15], 7) ^ rotate(words[i - 15], 18) ^
            (words[i - 15] >> 3);
        uint32_t s1 = rotate(words[i - 2], 17) ^ rotate(words[i - 2], 19) ^
            (words[i - 2] >> 10);
        words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }
    uint32_t a=value->state[0], b=value->state[1], c=value->state[2], d=value->state[3];
    uint32_t e=value->state[4], f=value->state[5], g=value->state[6], h=value->state[7];
    for (int i = 0; i < 64; ++i) {
        uint32_t s1=rotate(e,6)^rotate(e,11)^rotate(e,25), choice=(e&f)^(~e&g);
        uint32_t first=h+s1+choice+constants[i]+words[i];
        uint32_t s0=rotate(a,2)^rotate(a,13)^rotate(a,22), majority=(a&b)^(a&c)^(b&c);
        uint32_t second=s0+majority;
        h=g; g=f; f=e; e=d+first; d=c; c=b; b=a; a=first+second;
    }
    value->state[0]+=a; value->state[1]+=b; value->state[2]+=c; value->state[3]+=d;
    value->state[4]+=e; value->state[5]+=f; value->state[6]+=g; value->state[7]+=h;
}

static void update(Sha256 *value, const unsigned char *data, size_t size) {
    value->bits += (uint64_t)size * 8;
    while (size) {
        size_t take = 64 - value->used;
        if (take > size) take = size;
        memcpy(value->block + value->used, data, take);
        value->used += take; data += take; size -= take;
        if (value->used == 64) { transform(value); value->used = 0; }
    }
}

static void finish(Sha256 *value, char output[65]) {
    uint64_t bits = value->bits;
    unsigned char tail[72] = {0x80};
    size_t padding = value->used < 56 ? 56 - value->used : 120 - value->used;
    update(value, tail, padding);
    for (int i = 0; i < 8; ++i) tail[i] = (unsigned char)(bits >> (56 - i * 8));
    update(value, tail, 8);
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 32; ++i) {
        unsigned byte = (value->state[i / 4] >> (24 - (i % 4) * 8)) & 0xff;
        output[i * 2] = hex[byte >> 4]; output[i * 2 + 1] = hex[byte & 15];
    }
    output[64] = '\0';
}

static void initialize(Sha256 *value) {
    const uint32_t initial[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
        0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    memset(value, 0, sizeof(*value)); memcpy(value->state, initial, sizeof(initial));
}

void bongo_cat_sha256_bytes(const void *data, size_t size, char output[65]) {
    Sha256 value; initialize(&value); update(&value, data, size); finish(&value, output);
}

BongoCatResult bongo_cat_sha256_file(const char *path, char output[65], BongoCatError *error) {
    FILE *file = path ? bongo_cat_file_open(path, "rb") : NULL;
    if (!file) { bongo_cat_error_set(error, BONGO_CAT_ERROR_IO, "Cannot open file"); return BONGO_CAT_ERROR_IO; }
    Sha256 value; initialize(&value);
    unsigned char buffer[8192]; size_t count;
    while ((count = fread(buffer, 1, sizeof(buffer), file)) > 0) update(&value, buffer, count);
    bool ok = !ferror(file);
    if (fclose(file) != 0) ok = false;
    if (!ok) { bongo_cat_error_set(error, BONGO_CAT_ERROR_IO, "Cannot read file"); return BONGO_CAT_ERROR_IO; }
    finish(&value, output); return BONGO_CAT_OK;
}
