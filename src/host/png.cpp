#include "host/png.h"
#include "core/palette.h"
#include <stdio.h>
#include <string.h>
#include <vector>

namespace cbhost {
namespace {

uint32_t crc32_of(const uint8_t* d, size_t n, uint32_t crc = 0xFFFFFFFFu) {
    static uint32_t tbl[256];
    static bool built = false;
    if (!built) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            tbl[i] = c;
        }
        built = true;
    }
    for (size_t i = 0; i < n; i++) crc = tbl[(crc ^ d[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

void be32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(uint8_t(x >> 24)); v.push_back(uint8_t(x >> 16));
    v.push_back(uint8_t(x >> 8));  v.push_back(uint8_t(x));
}

void chunk(std::vector<uint8_t>& out, const char tag[4], const std::vector<uint8_t>& data) {
    be32(out, uint32_t(data.size()));
    std::vector<uint8_t> body(tag, tag + 4);
    body.insert(body.end(), data.begin(), data.end());
    out.insert(out.end(), body.begin(), body.end());
    be32(out, crc32_of(body.data(), body.size()) ^ 0xFFFFFFFFu);
}

// Deflate with stored blocks only: no compression, always valid.
std::vector<uint8_t> deflate_stored(const std::vector<uint8_t>& raw) {
    std::vector<uint8_t> z;
    z.push_back(0x78); z.push_back(0x01);            // zlib header, no preset dict
    size_t off = 0;
    while (off < raw.size()) {
        const size_t n = (raw.size() - off > 65535) ? 65535 : raw.size() - off;
        const bool last = (off + n == raw.size());
        z.push_back(last ? 1 : 0);
        z.push_back(uint8_t(n & 0xFF)); z.push_back(uint8_t(n >> 8));
        z.push_back(uint8_t(~n & 0xFF)); z.push_back(uint8_t((~n >> 8) & 0xFF));
        z.insert(z.end(), raw.begin() + off, raw.begin() + off + n);
        off += n;
    }
    uint32_t a = 1, b = 0;
    for (uint8_t byte : raw) { a = (a + byte) % 65521; b = (b + a) % 65521; }
    be32(z, (b << 16) | a);
    return z;
}

}  // namespace

bool write_png_rgb(const char* path, const uint8_t* rgb, int w, int h) {
    std::vector<uint8_t> raw;
    raw.reserve(size_t(h) * (1 + size_t(w) * 3));
    for (int y = 0; y < h; y++) {
        raw.push_back(0);                                  // filter type: none
        const uint8_t* row = rgb + size_t(y) * w * 3;
        raw.insert(raw.end(), row, row + size_t(w) * 3);
    }

    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<uint8_t> ihdr;
    be32(ihdr, uint32_t(w)); be32(ihdr, uint32_t(h));
    ihdr.push_back(8); ihdr.push_back(2); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", deflate_stored(raw));
    chunk(out, "IEND", {});

    FILE* f = fopen(path, "wb");
    if (!f) return false;
    const bool ok = fwrite(out.data(), 1, out.size(), f) == out.size();
    fclose(f);
    return ok;
}

bool write_png_from_canvas(const char* path, const cb::Canvas& c) {
    const int w = c.width(), h = c.height();
    std::vector<uint8_t> rgb(size_t(w) * h * 3);
    for (int i = 0; i < w * h; i++) {
        const cb::Rgb p = cb::palette_rgb(c.data()[i]);
        rgb[size_t(i) * 3 + 0] = p.r;
        rgb[size_t(i) * 3 + 1] = p.g;
        rgb[size_t(i) * 3 + 2] = p.b;
    }
    return write_png_rgb(path, rgb.data(), w, h);
}

}  // namespace cbhost
