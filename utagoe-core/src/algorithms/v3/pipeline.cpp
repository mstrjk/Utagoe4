// 書き出し側の stream 処理。元実装は block ごとに By Frequency エンジン、Centralization、LPF、HPF、soft clip を
// 書き出す範囲だけに掛ける。ここではその範囲を順につないでためておき、まとめて流す。
// どの段も状態を持った stream 処理なので、区切り方を変えても結果は同じになる。

#include "engine.h"
#include "log.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

namespace utagoe {
namespace v3 {
namespace {

// 一度にためる sample 数。4 分の曲で数回に分かれる程度にして、memory を抑えつつ並列化の単位を大きくする。
constexpr std::size_t kChunk = std::size_t{1} << 20;

}

void Pipeline::init(Context& c, bool freqEngine) {
    const Params& p = c.p;
    c_ = &c;
    ch_ = c.channels;
    quantize_ = c.quantize;
    freq_ = freqEngine;
    mono_ = freq_ && p.procMode == 2;

    // 元実装 0x40e4b8。channel ごとにエンジンを持つ。
    if (freq_)
        for (int k = 0; k < ch_; ++k) {
            fe_[k].init(8192, 8, p.soundQty, p.kvol, quantize_);
            fe_[k].setGpu(c.gpuFast);
        }
    // 元実装の対応箇所: 0x40e49c
    hasCntr_ = p.centralize;
    if (hasCntr_) {
        vf_.init(8192, 8, p.cntr, quantize_);
        vf_.setGpu(c.gpuFast);
    }
    // 元実装 0x40e4fc。遷移帯は hz..1.2hz、減衰 80 dB。
    hasLpf_ = p.lowPass;
    if (hasLpf_) {
        const float lo = static_cast<float>(p.lowPassHz);
        const float hi = static_cast<float>(static_cast<long double>(lo) * 1.2);
        for (int k = 0; k < ch_; ++k) {
            lpf_[k].design(c.rate, lo, hi, 80.0f, FirFilter::Mode::LowPass);
            lpf_[k].setGpu(c.gpuFast);
        }
    }
    // 元実装 0x40e54c。遷移帯は 0.75hz..hz、減衰 60 dB。
    hasHpf_ = p.highPass;
    if (hasHpf_) {
        const float hi = static_cast<float>(p.highPassHz);
        const float lo = static_cast<float>(static_cast<long double>(hi) * 0.75f);
        for (int k = 0; k < ch_; ++k) {
            hpf_[k].design(c.rate, lo, hi, 60.0f, FirFilter::Mode::HighPass);
            hpf_[k].setGpu(c.gpuFast);
        }
    }
}

void Pipeline::pushOutput(const double* l, const double* r, int count) {
    pre_[0].insert(pre_[0].end(), l, l + count);
    pre_[1].insert(pre_[1].end(), r, r + count);
    maybeFlush();
}

void Pipeline::pushEngine(const double* const* orig, const double* const* inst, int count, double level) {
    const int n = mono_ ? 1 : ch_;
    for (int k = 0; k < n; ++k) {
        for (int i = 0; i < count; ++i) {
            ea_[k].push_back(static_cast<float>(orig[k][i]));
            eb_[k].push_back(static_cast<float>(inst[k][i]));
        }
    }
    level_.insert(level_.end(), static_cast<std::size_t>(count), level);
    maybeFlush();
}

void Pipeline::maybeFlush() {
    if ((freq_ ? ea_[0].size() : pre_[0].size()) >= kChunk) flush();
}

void Pipeline::flush() {
    const std::size_t n = freq_ ? ea_[0].size() : pre_[0].size();
    if (n == 0) return;
    const int cnt = static_cast<int>(n);
    const auto t0 = std::chrono::steady_clock::now();
    auto since = [&] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); };
    std::string steps;

    // By Frequency。mono は左のエンジンだけを通して右へ複製する (0x40f638)。
    if (freq_) {
        const int engines = mono_ ? 1 : ch_;
        for (int k = 0; k < 2; ++k) pre_[k].assign(n, 0.0);
        for (int k = 0; k < engines; ++k)
            fe_[k].process(ea_[k].data(), eb_[k].data(), level_.data(), pre_[k].data(), cnt);
        if (mono_) pre_[1] = pre_[0];
        steps += " | Frequency " + std::to_string(static_cast<int>(since())) + " ms";
    } else {
        pre_[1].resize(n, 0.0);
    }

    if (c_->tapRaw)
        for (std::size_t i = 0; i < n; ++i)
            for (int k = 0; k < ch_; ++k) c_->rawTap.push_back(static_cast<float>(pre_[k][i]));

    // 元実装 0x40f748。抽出結果の L/R をその場で Centralization する。
    if (hasCntr_) {
        fl_.resize(n);
        fr_.resize(n);
        for (std::size_t i = 0; i < n; ++i) {
            fl_[i] = static_cast<float>(pre_[0][i]);
            fr_[i] = static_cast<float>(pre_[1][i]);
        }
        vf_.process(fl_.data(), fr_.data(), fl_.data(), fr_.data(), cnt);
        for (std::size_t i = 0; i < n; ++i) {
            pre_[0][i] = fl_[i];
            pre_[1][i] = fr_[i];
        }
        steps += " | centralization";
    }

    // 元実装 0x40f78c / 0x40f7fc。HPF は元実装どおり符号を反転して書き戻す。
    tmp_.resize(n);
    if (hasLpf_) {
        for (int k = 0; k < ch_; ++k) {
            lpf_[k].process(tmp_.data(), pre_[k].data(), cnt, quantize_);
            std::copy(tmp_.begin(), tmp_.end(), pre_[k].begin());
        }
        steps += " | LPF " + std::to_string(lpf_[0].taps()) + " taps";
    }
    if (hasHpf_) {
        for (int k = 0; k < ch_; ++k) {
            hpf_[k].process(tmp_.data(), pre_[k].data(), cnt, quantize_);
            for (std::size_t i = 0; i < n; ++i) pre_[k][i] = -tmp_[i];
        }
        steps += " | HPF " + std::to_string(hpf_[0].taps()) + " taps";
    }

    // 元実装 0x40e728。±31129 を超えた分を半分に圧縮し (soft clip)、16-bit の範囲に収める。
    // 丸めない経路では圧縮だけ行い、clamp は出力形式に任せる。
    raw_.resize(n * static_cast<std::size_t>(ch_));
    for (std::size_t i = 0; i < n; ++i) {
        for (int k = 0; k < ch_; ++k) {
            double x = pre_[k][i];
            if (quantize_) {
                if (x > 31129) x = std::trunc((x - 31129) / 2) + 31129;
                if (x < -31129) x = std::trunc((x + 31129) / 2) - 31129;
                x = std::clamp(x, -32768.0, 32767.0);
            } else {
                if (x > 31129) x = (x - 31129) * 0.5 + 31129;
                if (x < -31129) x = (x + 31129) * 0.5 - 31129;
            }
            raw_[i * static_cast<std::size_t>(ch_) + static_cast<std::size_t>(k)] = static_cast<float>(x);
        }
    }
    c_->out.write(raw_.data(), cnt);
    log::detail("  wrote %s of audio%s | %.0f ms", log::clock(static_cast<double>(n) / c_->rate).c_str(), steps.c_str(), since());

    for (int k = 0; k < 2; ++k) {
        ea_[k].clear();
        eb_[k].clear();
        pre_[k].clear();
    }
    level_.clear();
}

}
}
