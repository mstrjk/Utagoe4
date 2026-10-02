// 元実装の作業 object とその操作 (0x40ddec..0x40f86c) を写した。
// 各関数の先頭に対応する address を書いている。
// quantize のとき、x87 が拡張精度で計算して整数へ丸める箇所は long double で評価してから丸める。

#include "engine.h"
#include "parallel.h"
#include "gpu.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace utagoe {
namespace v3 {

int Source::read(float* dst, int count) {
    const std::size_t ch = static_cast<std::size_t>(channels);
    for (int f = 0; f < count; ++f) {
        const long long p = pos + f;
        float* d = dst + static_cast<std::size_t>(f) * ch;
        if (p >= 0 && p < frames) std::memcpy(d, data + static_cast<std::size_t>(p) * ch, ch * sizeof(float));
        else std::fill(d, d + ch, 0.0f);
    }
    if (pos >= frames) return 0;
    const long long got = pos < 0 ? count : std::min<long long>(count, frames - pos);
    pos += got;
    return static_cast<int>(got);
}

void Sink::write(const float* src, int frames) {
    data.insert(data.end(), src, src + static_cast<std::size_t>(frames) * static_cast<std::size_t>(channels));
}

bool Context::poll(double fraction) {
    if (cancelled) return false;
    if (progress) {
        const double f = std::clamp(fraction, 0.0, 1.0);
        if (!progress(static_cast<float>(progressFrom + (progressTo - progressFrom) * f), progressUser))
            cancelled = true;
    }
    return !cancelled;
}


// 元実装の対応箇所: 0x40e150
void Worker::init(int channels, int rate, double blockSec, int ovs, bool quantize) {
    ch_ = channels;
    rate_ = rate;
    ovs_ = ovs > 0 ? ovs : 1;
    quantize_ = quantize;
    phaseSign_ = 1.0;
    n_ = static_cast<int>(std::nearbyint(static_cast<long double>(rate) * blockSec));
    m_ = static_cast<int>(std::nearbyint(static_cast<long double>(rate) * blockSec * 1.5f));

    // 元実装は malloc で未初期化のまま使う箇所がある。ここでは 0 で始める。
    const std::size_t n = static_cast<std::size_t>(n_), m = static_cast<std::size_t>(m_);
    const std::size_t c = static_cast<std::size_t>(ch_);
    rawOrig_.assign(n * c, 0.0f);
    rawInst_.assign(m * c, 0.0f);
    for (int k = 0; k < 2; ++k) {
        o_[k].assign(n, 0.0);
        i_[k].assign(m, 0.0);
        io_[k].assign(m * static_cast<std::size_t>(ovs_), 0.0);
        out_[k].assign(n, 0.0);
    }
    om_.assign(n, 0.0);
    im_.assign(m, 0.0);
    gpuKind_ = 0;
    imo_.assign(m * static_cast<std::size_t>(ovs_), 0.0);
}


// 元実装の対応箇所: 0x40e59c
void Worker::reset() {
    gpuKind_ = 0;
    for (int k = 0; k < 2; ++k) {
        std::fill(o_[k].begin(), o_[k].end(), 0.0);
        std::fill(i_[k].begin(), i_[k].end(), 0.0);
    }
    std::fill(rawOrig_.begin(), rawOrig_.end(), 0.0f);
    std::fill(rawInst_.begin(), rawInst_.end(), 0.0f);
    std::fill(om_.begin(), om_.end(), 0.0);
    std::fill(im_.begin(), im_.end(), 0.0);
}

// 元実装 0x40e668。interleaved を channel ごとに分け、インストに極性を掛ける。mono の右は 0。
void Worker::prepare() {
    gpuKind_ = 0;
    const std::size_t c = static_cast<std::size_t>(ch_);
    for (int i = 0; i < n_; ++i) {
        o_[0][static_cast<std::size_t>(i)] = rawOrig_[static_cast<std::size_t>(i) * c];
        o_[1][static_cast<std::size_t>(i)] = ch_ > 1 ? rawOrig_[static_cast<std::size_t>(i) * c + 1] : 0.0;
    }
    for (int i = 0; i < m_; ++i) {
        i_[0][static_cast<std::size_t>(i)] = rawInst_[static_cast<std::size_t>(i) * c] * phaseSign_;
        i_[1][static_cast<std::size_t>(i)] = ch_ > 1 ? rawInst_[static_cast<std::size_t>(i) * c + 1] * phaseSign_ : 0.0;
    }
}

// 元実装 0x40e820。L - R。
void Worker::toLRDiff() {
    gpuKind_ = 0;
    for (int i = 0; i < n_; ++i) om_[static_cast<std::size_t>(i)] = o_[0][static_cast<std::size_t>(i)] - o_[1][static_cast<std::size_t>(i)];
    for (int i = 0; i < m_; ++i) im_[static_cast<std::size_t>(i)] = i_[0][static_cast<std::size_t>(i)] - i_[1][static_cast<std::size_t>(i)];
}

// 元実装 0x40e87c。L + R。
void Worker::toMono() {
    gpuKind_ = 0;
    for (int i = 0; i < n_; ++i) om_[static_cast<std::size_t>(i)] = o_[0][static_cast<std::size_t>(i)] + o_[1][static_cast<std::size_t>(i)];
    for (int i = 0; i < m_; ++i) im_[static_cast<std::size_t>(i)] = i_[0][static_cast<std::size_t>(i)] + i_[1][static_cast<std::size_t>(i)];
}

namespace {

// 元実装 0x40e8d8 / 0x40e980。の線形補間。out[i*ovs+k] = x[i] + (x[i+1]-x[i]) * k/ovs。最後の sample の後ろは書かない。
// ovs が 2 のべき乗なら k/ovs も積も和も拡張精度で誤差なく表せるので、整数演算の最近接偶数丸めと結果が一致する。
// その場合は速い整数版を使う。16-bit 以外の経路では値が 1/256 sample の格子に乗っているので、
// 同じ整数版をその格子で使い、結果も格子に丸める (GPU と完全に同じ値になる)。
void interpolate(const std::vector<double>& x, std::vector<double>& y, int m, int ovs, bool quantize) {
    if ((ovs & (ovs - 1)) == 0) {
        const double sc = quantize ? 1.0 : 256.0;
        int shift = 0;
        while ((1 << shift) < ovs) ++shift;
        const long long half = ovs >> 1;
        parallelFor(m - 1, 2048, [&](long long b, long long e) {
            for (long long i = b; i < e; ++i) {
                const long long a = static_cast<long long>(x[static_cast<std::size_t>(i)] * sc);
                const long long d = static_cast<long long>(x[static_cast<std::size_t>(i) + 1] * sc) - a;
                double* out = y.data() + i * ovs;
                for (int k = 0; k < ovs; ++k) {
                    const long long num = d * k;               // 値は a + num / ovs
                    long long qt = num >> shift;
                    const long long r = num - (qt << shift);   // 余り r は 0 <= r < ovs。
                    if (r > half || (r == half && half != 0 && ((a + qt) & 1))) ++qt;
                    out[k] = static_cast<double>(a + qt) / sc;
                }
            }
        });
        return;
    }
    parallelFor(m - 1, 2048, [&](long long b, long long e) {
        for (long long i = b; i < e; ++i) {
            const double a = x[static_cast<std::size_t>(i)];
            const double d = x[static_cast<std::size_t>(i) + 1] - a;
            double* out = y.data() + i * ovs;
            for (int k = 0; k < ovs; ++k) {
                const long double v = static_cast<long double>(k) / ovs * d + a;
                out[k] = quantize ? static_cast<double>(rint87(v)) : static_cast<double>(v);
            }
        }
    });
}

}

void Worker::upsample() {
    for (int c = 0; c < ch_; ++c) interpolate(i_[c], io_[c], m_, ovs_, quantize_);
}

void Worker::upsampleMix() {
    interpolate(im_, imo_, m_, ovs_, quantize_);
}

namespace {

// 候補を元実装の試行順に並べて score を計算し、最初に現れた最小値の候補を返す (元実装は厳密に小さいときだけ更新する)。
// 候補は並列に計算する。各 thread は自分の中の最良値で打ち切るが、最小の score は打ち切られないので結果は直列と同じ。
// score(index, 打ち切り値) は打ち切ったら打ち切り値より大きい値を返す。
template <class Score>
int pickBest(int count, long long workPerCandidate, std::vector<double>& scores, Score&& score) {
    scores.assign(static_cast<std::size_t>(count), std::numeric_limits<double>::infinity());
    auto run = [&](long long b, long long e) {
        double local = std::numeric_limits<double>::infinity();
        for (long long k = b; k < e; ++k) {
            const double v = score(static_cast<int>(k), local);
            scores[static_cast<std::size_t>(k)] = v;
            if (v < local) local = v;
        }
    };
    // 小さい仕事は分けない。早期打ち切りが効く直列の方が速い。
    if (static_cast<long long>(count) * workPerCandidate < 1500000) run(0, count);
    else parallelFor(count, std::max<long long>(1, 250000 / std::max<long long>(1, workPerCandidate)), run);
    int best = -1;
    double bestScore = std::numeric_limits<double>::infinity();
    for (int k = 0; k < count; ++k) {
        if (scores[static_cast<std::size_t>(k)] < bestScore) {
            bestScore = scores[static_cast<std::size_t>(k)];
            best = k;
        }
    }
    return best;
}

}


// 元実装 0x40eaf0 / 0x40ed88。base から ±k (k = 0..range, step 刻み) の候補を +k, -k の順に試す。
// score = sum |d| + |d - d_prev| (d = 原曲 - インスト、channel ごとに d_prev は 0 から)。
// 厳密に小さい score だけが更新するので、同点なら先に試した候補が残る。
int Worker::searchCore(const double* const* o, const double* const* in, int nch, int count,
                       int base, int range, int step, int stride, bool mix) {
    const long long limit = static_cast<long long>(stride == 1 ? m_ : m_ * ovs_);
    if (step < 1) step = 1;

    cand_.clear();
    for (int k = 0; k <= range; k += step) {
        cand_.push_back(base + k);
        if (k != 0) cand_.push_back(base - k);
    }
    auto valid = [&](int pos) { return pos >= 0 && pos + static_cast<long long>(stride) * (count - 1) < limit; };

    // GPU は score を整数で正確に求めるので、同じ試行順で最初の最小値を選べば CPU と同じ位置になる。
    const long long work = static_cast<long long>(cand_.size()) * count * nch;
    if (gpuSearch_ && work >= gpuMinWork_ && (stride & (stride - 1)) == 0 && gpuBlock(mix, count)) {
        gpuCand_.clear();
        for (int pos : cand_)
            if (valid(pos)) gpuCand_.push_back(pos);
        if (gpuCand_.empty()) return base;
        if (gpu::searchScores(stride, stride, gpuCand_.data(), static_cast<int>(gpuCand_.size()), gpuScores_)) {
            int best = 0;
            for (std::size_t k = 1; k < gpuCand_.size(); ++k)
                if (gpuScores_[k] < gpuScores_[static_cast<std::size_t>(best)]) best = static_cast<int>(k);
            return gpuCand_[static_cast<std::size_t>(best)];
        }
        gpuSearch_ = gpuFast_ = false;   // GPU が使えなくなった。以後は CPU で続ける。
    }

    const int best = pickBest(static_cast<int>(cand_.size()), static_cast<long long>(count) * nch, scores_,
                              [&](int idx, double cut) {
        const int pos = cand_[static_cast<std::size_t>(idx)];
        // 元実装は範囲外も読んでしまう。ここでは buffer に収まらない候補を飛ばす。
        if (!valid(pos)) return std::numeric_limits<double>::infinity();
        double s = 0.0;
        for (int c = 0; c < nch && s <= cut; ++c) {
            double prev = 0.0;
            const double* a = o[c];
            const double* b = in[c] + pos;
            for (int i = 0; i < count; ++i) {
                const double d = a[i] - b[static_cast<std::ptrdiff_t>(i) * stride];
                s += std::fabs(d) + std::fabs(d - prev);
                prev = d;
                if (s > cut) break;
            }
        }
        return s;
    });
    return best < 0 ? base : cand_[static_cast<std::size_t>(best)];
}

bool Worker::gpuBlock(bool mix, int count) {
    const int kind = mix ? 2 : 1;
    if (gpuKind_ == kind && gpuCount_ == count) return true;
    const int nch = mix ? 1 : ch_;
    const double sc = scale();
    const int32_t* o[2] = {nullptr, nullptr};
    const int32_t* in[2] = {nullptr, nullptr};
    for (int c = 0; c < nch; ++c) {
        const std::vector<double>& src = mix ? om_ : o_[c];
        const std::vector<double>& ins = mix ? im_ : i_[c];
        std::vector<int32_t>& a = gpuInt_[2 * c];
        std::vector<int32_t>& b = gpuInt_[2 * c + 1];
        a.resize(static_cast<std::size_t>(count));
        b.resize(static_cast<std::size_t>(m_));
        for (int i = 0; i < count; ++i) a[static_cast<std::size_t>(i)] = static_cast<int32_t>(src[static_cast<std::size_t>(i)] * sc);
        for (int i = 0; i < m_; ++i) b[static_cast<std::size_t>(i)] = static_cast<int32_t>(ins[static_cast<std::size_t>(i)] * sc);
        o[c] = a.data();
        in[c] = b.data();
    }
    if (!gpu::setBlock(o, nch, count, in, m_)) return false;
    gpuKind_ = kind;
    gpuCount_ = count;
    return true;
}

// 元実装の対応箇所: 0x40ecf0
int Worker::searchCh(int base, int range, int step) {
    const double* o[2] = {o_[0].data(), o_[1].data()};
    const double* in[2] = {i_[0].data(), i_[1].data()};
    return searchCore(o, in, ch_, n_, base, range, step, 1, false);
}

// 元実装 0x40ed2c。(count = N) / 0x40ed5c (count 指定)
int Worker::searchMix(int base, int range, int step, int count) {
    const double* o[1] = {om_.data()};
    const double* in[1] = {im_.data()};
    return searchCore(o, in, 1, count, base, range, step, 1, true);
}

// 元実装の対応箇所: 0x40efac
int Worker::searchOvsCh(int base, int range, int step) {
    const double* o[2] = {o_[0].data(), o_[1].data()};
    const double* in[2] = {io_[0].data(), io_[1].data()};
    return searchCore(o, in, ch_, n_, base, range, step, ovs_, false);
}

// 元実装の対応箇所: 0x40efe0
int Worker::searchOvsMix(int base, int range, int step) {
    const double* o[1] = {om_.data()};
    const double* in[1] = {imo_.data()};
    return searchCore(o, in, 1, n_, base, range, step, ovs_, true);
}

// 元実装 0x40e9f4。mix の |d| だけで探す。DetailOffset の envelope 照合で使う。
int Worker::searchMixAbs(int base, int range, int step) {
    const long long limit = static_cast<long long>(im_.size());
    double best = std::numeric_limits<double>::infinity();
    int bestPos = base;
    auto tryPos = [&](int pos) {
        if (pos < 0 || pos + static_cast<long long>(n_) > limit) return;
        double s = 0.0;
        for (int i = 0; i < n_; ++i) {
            s += std::fabs(om_[static_cast<std::size_t>(i)] - im_[static_cast<std::size_t>(pos + i)]);
            if (s > best) break;
        }
        if (s < best) {
            best = s;
            bestPos = pos;
        }
    };
    if (step < 1) step = 1;
    for (int k = 0; k <= range; k += step) {
        tryPos(base + k);
        if (k != 0) tryPos(base - k);
    }
    return bestPos;
}


// 元実装 0x40f008。g = center, center±step, center±2step ... (|delta| <= maxDelta) を +, - の順に試し、
// sum |round(原曲 - インスト * g)| が最小の g を返す。
double Worker::levelCore(const double* const* o, const double* const* in, int nch, int pos,
                         double step, double maxDelta, double center, bool mix) {
    if (pos < 0 || pos + n_ > m_) return center;

    gains_.clear();
    for (double delta = 0.0; delta <= maxDelta; delta = static_cast<double>(static_cast<long double>(step) + delta)) {
        gains_.push_back(static_cast<double>(static_cast<long double>(center) + delta));
        if (delta != 0.0) gains_.push_back(static_cast<double>(static_cast<long double>(center) - delta));
    }
    double g;
    if (gpuLevel(mix, pos, 1, g)) return g;
    const int best = pickBest(static_cast<int>(gains_.size()), static_cast<long long>(n_) * nch, scores_,
                              [&](int idx, double cut) {
        const double g = gains_[static_cast<std::size_t>(idx)];
        double s = 0.0;
        for (int i = 0; i < n_ && s <= cut; ++i) {
            for (int c = 0; c < nch; ++c) {
                const long double d = o[c][i] - static_cast<long double>(in[c][pos + i]) * g;
                s += std::fabs(static_cast<double>(quantize_ ? rint87(d) : d));
            }
        }
        return s;
    });
    return best < 0 ? center : gains_[static_cast<std::size_t>(best)];
}

// 元実装 0x40f22c。oversampling 版は center - maxDelta から center + maxDelta へ昇順に試す。
double Worker::levelOvsCore(const double* const* o, const double* const* in, int nch, int pos,
                            double step, double maxDelta, double center, bool mix) {
    if (pos < 0 || pos + static_cast<long long>(ovs_) * (n_ - 1) >= static_cast<long long>(m_) * ovs_) return center;

    gains_.clear();
    const double hi = static_cast<double>(static_cast<long double>(center) + maxDelta);
    for (double g = static_cast<double>(static_cast<long double>(center) - maxDelta); g <= hi;
         g = static_cast<double>(static_cast<long double>(step) + g))
        gains_.push_back(g);
    double gpuG;
    if ((ovs_ & (ovs_ - 1)) == 0 && gpuLevel(mix, pos, ovs_, gpuG)) return gpuG;
    const int best = pickBest(static_cast<int>(gains_.size()), static_cast<long long>(n_) * nch, scores_,
                              [&](int idx, double cut) {
        const double g = gains_[static_cast<std::size_t>(idx)];
        double s = 0.0;
        for (int i = 0; i < n_ && s <= cut; ++i) {
            for (int c = 0; c < nch; ++c) {
                const long double d = o[c][i] - static_cast<long double>(in[c][pos + static_cast<std::ptrdiff_t>(ovs_) * i]) * g;
                s += std::fabs(static_cast<double>(quantize_ ? rint87(d) : d));
            }
        }
        return s;
    });
    return best < 0 ? center : gains_[static_cast<std::size_t>(best)];
}

double Worker::levelFitCh(int pos, double step, double maxDelta, double center) {
    const double* o[2] = {o_[0].data(), o_[1].data()};
    const double* in[2] = {i_[0].data(), i_[1].data()};
    return levelCore(o, in, ch_, pos, step, maxDelta, center, false);
}

double Worker::levelFitMix(int pos, double step, double maxDelta, double center) {
    const double* o[1] = {om_.data()};
    const double* in[1] = {im_.data()};
    return levelCore(o, in, 1, pos, step, maxDelta, center, true);
}

double Worker::levelFitOvsCh(int pos, double step, double maxDelta, double center) {
    const double* o[2] = {o_[0].data(), o_[1].data()};
    const double* in[2] = {io_[0].data(), io_[1].data()};
    return levelOvsCore(o, in, ch_, pos, step, maxDelta, center, false);
}

double Worker::levelFitOvsMix(int pos, double step, double maxDelta, double center) {
    const double* o[1] = {om_.data()};
    const double* in[1] = {imo_.data()};
    return levelOvsCore(o, in, 1, pos, step, maxDelta, center, true);
}

// 速度優先のときだけ level 推定を GPU で行う。gains_ の候補から最初の最小値を選ぶ。
bool Worker::gpuLevel(bool mix, int pos, int stride, double& g) {
    if (!gpuFast_ || !gpuBlock(mix, n_)) return false;
    gpuGains_.assign(gains_.begin(), gains_.end());
    if (!gpu::levelScores(stride, stride, pos, static_cast<float>(scale()), quantize_, gpuGains_.data(),
                          static_cast<int>(gpuGains_.size()), scores_)) {
        gpuSearch_ = gpuFast_ = false;
        return false;
    }
    int best = 0;
    for (std::size_t k = 1; k < scores_.size(); ++k)
        if (scores_[k] < scores_[static_cast<std::size_t>(best)]) best = static_cast<int>(k);
    g = gains_[static_cast<std::size_t>(best)];
    return true;
}


// 元実装 0x40f3a0 / 0x40f498。左右とも out = round(原曲 - インスト * g)。
void Worker::subCore(const double* i0, const double* i1, int pos, int stride, double g) {
    for (int i = 0; i < n_; ++i) {
        const std::ptrdiff_t j = pos + static_cast<std::ptrdiff_t>(stride) * i;
        out_[0][static_cast<std::size_t>(i)] = q(o_[0][static_cast<std::size_t>(i)] - static_cast<long double>(i0[j]) * g);
        out_[1][static_cast<std::size_t>(i)] = q(o_[1][static_cast<std::size_t>(i)] - static_cast<long double>(i1[j]) * g);
    }
}

// 元実装 0x40f410 / 0x40f514。out = round(((oL + oR) - (iL + iR) * g) * 0.5) を左右両方へ。
void Worker::subMonoCore(const double* i0, const double* i1, int pos, int stride, double g) {
    for (int i = 0; i < n_; ++i) {
        const std::ptrdiff_t j = pos + static_cast<std::ptrdiff_t>(stride) * i;
        const double a = o_[0][static_cast<std::size_t>(i)] + o_[1][static_cast<std::size_t>(i)];
        const double b = i0[j] + i1[j];
        const double v = q((a - static_cast<long double>(b) * g) * 0.5f);
        out_[0][static_cast<std::size_t>(i)] = v;
        out_[1][static_cast<std::size_t>(i)] = v;
    }
}

void Worker::sub(int pos, double g) { subCore(i_[0].data(), i_[1].data(), pos, 1, g); }
void Worker::subMono(int pos, double g) { subMonoCore(i_[0].data(), i_[1].data(), pos, 1, g); }
void Worker::subOvs(int pos, double g) { subCore(io_[0].data(), io_[1].data(), pos, ovs_, g); }
void Worker::subOvsMono(int pos, double g) { subMonoCore(io_[0].data(), io_[1].data(), pos, ovs_, g); }

// 元実装 0x40f638。の前半。左右の平均 (整数除算は 0 方向へ切り捨て) を左 channel に置く。後半のエンジン処理は Pipeline が行う。
void Worker::monoAverage(int start, int count, int pos) {
    gpuKind_ = 0;
    for (int i = start; i < start + count; ++i) {
        const std::size_t a = static_cast<std::size_t>(i), b = static_cast<std::size_t>(pos + i);
        const double so = o_[0][a] + o_[1][a], si = i_[0][b] + i_[1][b];
        o_[0][a] = quantize_ ? std::trunc(so / 2) : so / 2;
        i_[0][b] = quantize_ ? std::trunc(si / 2) : si / 2;
    }
}

double Worker::sumAbsOut(int from, int to) const {
    double s = 0.0;
    for (int i = from; i < to; ++i)
        s += std::fabs(out_[0][static_cast<std::size_t>(i)]) + std::fabs(out_[1][static_cast<std::size_t>(i)]);
    return s;
}

double Worker::sumAbsOrig(int from, int to) const {
    double s = 0.0;
    for (int i = from; i < to; ++i)
        s += std::fabs(o_[0][static_cast<std::size_t>(i)]) + std::fabs(o_[1][static_cast<std::size_t>(i)]);
    return s;
}

}
}
