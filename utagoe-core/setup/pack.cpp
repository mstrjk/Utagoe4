// 配布用 setup に埋め込む Utagoe 本体を圧縮する build 用の道具。
// 形式: "UTGZ" + 元の大きさ (u64, little endian) + LZMS で圧縮した本体。展開は setup.cpp。
// LZMS は Windows 8 以降の Compression API (cabinet.dll) にあり、setup 側に展開の code を持たなくてよい。

#include <windows.h>
#include <compressapi.h>

#include <cstdint>
#include <cstdio>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: utagoe_pack <input> <output>\n");
        return 2;
    }
    std::FILE* in = std::fopen(argv[1], "rb");
    if (!in) { std::fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }
    std::vector<unsigned char> data;
    unsigned char buf[65536];
    for (std::size_t n; (n = std::fread(buf, 1, sizeof buf, in)) > 0;) data.insert(data.end(), buf, buf + n);
    std::fclose(in);

    COMPRESSOR_HANDLE c = nullptr;
    if (!CreateCompressor(COMPRESS_ALGORITHM_LZMS, nullptr, &c)) { std::fprintf(stderr, "CreateCompressor failed\n"); return 1; }
    SIZE_T need = 0;
    Compress(c, data.data(), data.size(), nullptr, 0, &need);
    std::vector<unsigned char> packed(need);
    SIZE_T got = 0;
    if (!Compress(c, data.data(), data.size(), packed.data(), packed.size(), &got)) {
        std::fprintf(stderr, "Compress failed (%lu)\n", GetLastError());
        CloseCompressor(c);
        return 1;
    }
    CloseCompressor(c);

    std::FILE* out = std::fopen(argv[2], "wb");
    if (!out) { std::fprintf(stderr, "cannot write %s\n", argv[2]); return 1; }
    const std::uint64_t size = data.size();
    std::fwrite("UTGZ", 1, 4, out);
    std::fwrite(&size, sizeof size, 1, out);
    std::fwrite(packed.data(), 1, got, out);
    std::fclose(out);
    std::printf("packed %llu -> %llu bytes\n", static_cast<unsigned long long>(size), static_cast<unsigned long long>(got + 12));
    return 0;
}
