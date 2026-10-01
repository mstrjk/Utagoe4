// Utagoe v3 由来の複素 FFT インターフェース。
// 元バイナリの対応箇所は Utagoe-v3-full_en.exe、image base 0x400000。
// データは元実装に合わせて float の実部・虚部を交互に並べる。k 番目は data[2*k] / data[2*k+1]。
// 変換長は 2 のべき乗に限る。
// 逆アセンブルとの対応は NOTES.md を参照。

#ifndef UTAGOE_FFT_H
#define UTAGOE_FFT_H

#include <cstddef>
#include <vector>

namespace utagoe {

// 元実装は変換ごとに 2 本の作業バッファを持ち、長さが変わったときだけ作り直す。
// 1 本目は (int)(sqrt(n/2) + 2.0)、2 本目は n/2。ブロックサイズを設定値にしている理由もこれ。
class FftTables {
public:
    FftTables() = default;
    explicit FftTables(std::size_t n) { resize(n); }

    void resize(std::size_t n);

    std::size_t size() const { return n_; }

    const float* twiddles() const { return tw_.data(); }
    const int*   strides()  const { return stride_.data(); }

private:
    std::size_t        n_ = 0;
    std::vector<float> tw_;
    std::vector<int>   stride_;
};

void fft_forward(float* data, std::size_t n, const FftTables& tables);

// 逆変換後の 1/n 正規化は元実装に合わせて別パスで行う。forward->inverse は丸め誤差の範囲で元に戻る。
void fft_inverse(float* data, std::size_t n, const FftTables& tables);

}

#endif
