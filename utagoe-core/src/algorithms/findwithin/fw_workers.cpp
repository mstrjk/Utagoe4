#include "fw_internal.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <set>
#include <unordered_map>

namespace utagoe {
namespace fw {

const std::vector<std::pair<std::string, std::pair<std::string, double>>>& workerInfo() {
    static const std::vector<std::pair<std::string, std::pair<std::string, double>>> info = {
        {"pcm_hash", {"exact", 1.00}},
        {"landmark_pairs", {"landmark", .95}},
        {"spectral_simhash", {"timbre", .85}},
        {"mfcc_context", {"timbre", .75}},
        {"cens_chroma", {"harmonic", .70}},
        {"harmonic_ridges", {"harmonic", .80}},
        {"percussive_shingles", {"rhythm", .75}},
        {"onset_sax", {"shape", .65}},
        {"autocorr_lags", {"rhythm", .70}},
        {"bass_contour", {"harmonic", .70}},
        {"stereo_side", {"spatial", .90}},
        {"spectral_rank", {"timbre", .75}},
        {"modulation_haar", {"shape", .70}},
        {"mass_profile", {"shape", .80}},
        {"subsequence_dtw", {"sequence", .70}},
    };
    return info;
}

namespace {

struct Seeds {
    std::vector<Seed> seeds;
    double null = 0.0;
};

Seeds pcmWorker(const Audio& a, int sr, const Config& cfg) {
    Seeds out;
    const std::size_t n = a.frames();
    if (n < 64) return out;
    std::vector<std::uint64_t> q(n);
    for (std::size_t i = 0; i < n; ++i) {
        long long q0 = static_cast<long long>(std::nearbyint(std::clamp(a.at(i, 0), -8.0, 8.0) * 4096)) + 32769;
        if (a.channels == 2) {
            const long long q1 = static_cast<long long>(std::nearbyint(std::clamp(a.at(i, 1), -8.0, 8.0) * 4096)) + 32769;
            q0 = q0 * 65539 + q1;
        }
        q[i] = static_cast<std::uint64_t>(q0);
    }
    const std::size_t width = std::min<std::size_t>(n - 1, static_cast<std::size_t>(std::max(32, static_cast<int>(sr * .024))));
    const std::uint64_t selector = (std::uint64_t(1) << static_cast<int>(std::ceil(std::log2(std::max(32.0, sr * .008))))) - 1;
    const std::uint64_t base = 1099511628211ULL;
    std::uint64_t power = 1;
    for (std::size_t k = 0; k + 1 < width; ++k) power *= base;
    std::uint64_t h = 0;
    for (std::size_t k = 0; k < width; ++k) h = h * base + q[k];
    std::unordered_map<std::uint64_t, std::vector<double>> bins;
    const double gap = std::max(cfg.minSeparation, cfg.minRepeat), ctx = std::max(cfg.minRepeat, .4);
    for (std::size_t i = 0; i + width <= n; ++i) {
        if (i > 0) h = (h - q[i - 1] * power) * base + q[i + width - 1];
        if ((h & selector) != 0 || q[i] == q[i + width / 2]) continue;
        const double t = static_cast<double>(i) / sr;
        std::vector<double>& prev = bins[h];
        for (double u : prev)
            if (t - u >= gap) out.seeds.push_back({u, t, 1.0, ctx});
        if (static_cast<int>(prev.size()) < cfg.indexBucketCap) prev.push_back(t);
        else prev.back() = t;
    }
    return out;
}

double rowNorm(const Mat& p, int i) {
    double s = 0;
    for (int c = 0; c < p.cols; ++c) s += p.at(i, c) * p.at(i, c);
    return std::sqrt(s);
}

double rowDot(const Mat& p, int i, int j) {
    const double* a = p.row(i);
    const double* b = p.row(j);
    double s = 0;
    for (int c = 0; c < p.cols; ++c) s += a[c] * b[c];
    return s;
}

Seeds neighbors(const Mat& p, const std::vector<int>& idx, double dt, const Config& cfg, bool tree, double threshold,
                double ctx, const std::string& name) {
    Seeds out;
    const int n = p.rows;
    if (n < 3) return out;
    Rng rng(cfg.seed + crc32(name));
    const int pairsN = std::min(2048, n * 3);
    std::vector<int> ii(static_cast<std::size_t>(pairsN)), jj(static_cast<std::size_t>(pairsN));
    for (int& v : ii) v = static_cast<int>(rng.below(n));
    for (int& v : jj) v = static_cast<int>(rng.below(n));
    std::vector<double> null;
    for (int k = 0; k < pairsN; ++k)
        if (std::abs(idx[static_cast<std::size_t>(ii[static_cast<std::size_t>(k)])] - idx[static_cast<std::size_t>(jj[static_cast<std::size_t>(k)])]) * dt >
            std::max(cfg.minSeparation, ctx))
            null.push_back(rowDot(p, ii[static_cast<std::size_t>(k)], jj[static_cast<std::size_t>(k)]));
    out.null = null.empty() ? 0.0 : median(null);
    const double minsep = std::max({cfg.minSeparation, cfg.minRepeat, ctx * .8});
    std::vector<std::set<int>> pools(static_cast<std::size_t>(n));
    auto far = [&](int i, int j) { return std::abs(idx[static_cast<std::size_t>(i)] - idx[static_cast<std::size_t>(j)]) * dt >= minsep; };
    if (tree) {
        std::vector<double> norms(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) norms[static_cast<std::size_t>(i)] = rowDot(p, i, i);
        const int k = std::min(n, 24);
        std::vector<std::vector<int>> nns(static_cast<std::size_t>(n));
        parallelFor(n, 16, [&](long long b, long long e) {
            std::vector<std::pair<double, int>> d(static_cast<std::size_t>(n));
            for (long long i = b; i < e; ++i) {
                for (int j = 0; j < n; ++j)
                    d[static_cast<std::size_t>(j)] = {std::max(0.0, norms[static_cast<std::size_t>(i)] + norms[static_cast<std::size_t>(j)] -
                                                                       2 * rowDot(p, static_cast<int>(i), j)),
                                                      j};
                std::partial_sort(d.begin(), d.begin() + k, d.end());
                for (int r = 0; r < k; ++r) nns[static_cast<std::size_t>(i)].push_back(d[static_cast<std::size_t>(r)].second);
            }
        });
        for (int i = 0; i < n; ++i)
            for (int j : nns[static_cast<std::size_t>(i)])
                if (far(i, j)) pools[static_cast<std::size_t>(i)].insert(j);
    } else {
        const int bits = 9;
        for (int table = 0; table < 5; ++table) {
            std::vector<double> proj(static_cast<std::size_t>(p.cols) * bits);
            for (double& v : proj) v = static_cast<float>(rng.normal());
            std::vector<int> keys(static_cast<std::size_t>(n), 0);
            for (int i = 0; i < n; ++i) {
                int key = 0;
                for (int b = 0; b < bits; ++b) {
                    double s = 0;
                    for (int c = 0; c < p.cols; ++c) s += p.at(i, c) * proj[static_cast<std::size_t>(c) * bits + b];
                    if (s > 0) key |= 1 << b;
                }
                keys[static_cast<std::size_t>(i)] = key;
            }
            std::unordered_map<int, std::vector<int>> buckets;
            for (int i = 0; i < n; ++i) buckets[keys[static_cast<std::size_t>(i)]].push_back(i);
            for (int i = 0; i < n; ++i) {
                const int key = keys[static_cast<std::size_t>(i)];
                std::vector<int> cand = buckets[key];
                auto it = buckets.find(key ^ (1 << (table % bits)));
                if (it != buckets.end()) cand.insert(cand.end(), it->second.begin(), it->second.end());
                if (static_cast<int>(cand.size()) > cfg.indexBucketCap) {
                    std::vector<int> take;
                    const int cap = cfg.indexBucketCap;
                    for (int k = 0; k < cap; ++k)
                        take.push_back(cand[static_cast<std::size_t>(static_cast<double>(cand.size() - 1) * k / (cap - 1))]);
                    cand = take;
                }
                for (int j : cand)
                    if (far(i, j)) pools[static_cast<std::size_t>(i)].insert(j);
            }
        }
    }
    std::map<std::pair<int, int>, double> pairs;
    for (int i = 0; i < n; ++i) {
        const auto& pool = pools[static_cast<std::size_t>(i)];
        if (pool.empty() || rowNorm(p, i) < .5) continue;
        std::vector<std::pair<double, int>> sc;
        for (int j : pool) sc.push_back({std::clamp(rowDot(p, j, i), -1.0, 1.0), j});
        std::stable_sort(sc.begin(), sc.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (std::size_t z = sc.size() > 3 ? sc.size() - 3 : 0; z < sc.size(); ++z)
            if (sc[z].first >= threshold) {
                const int a = std::min(i, sc[z].second), b = std::max(i, sc[z].second);
                auto key = std::make_pair(a, b);
                auto it = pairs.find(key);
                pairs[key] = it == pairs.end() ? sc[z].first : std::max(it->second, sc[z].first);
            }
    }
    for (const auto& [key, s] : pairs)
        out.seeds.push_back({idx[static_cast<std::size_t>(key.first)] * dt, idx[static_cast<std::size_t>(key.second)] * dt, s, ctx});
    return out;
}

Seeds featureWorker(const Bank& bank, const Config& cfg, const std::string& name) {
    struct Def {
        const char* feature;
        bool tree;
        double threshold;
        bool center;
    };
    static const std::map<std::string, Def> defs = {
        {"spectral_simhash", {"spectral", false, .86, true}},  {"mfcc_context", {"mfcc", true, .84, true}},
        {"cens_chroma", {"cens", true, .87, false}},           {"harmonic_ridges", {"harmonic_ridges", false, .84, true}},
        {"percussive_shingles", {"percussion", false, .85, true}}, {"bass_contour", {"bass", true, .87, false}},
        {"stereo_side", {"side", false, .86, true}},           {"spectral_rank", {"rank", false, .84, true}},
        {"modulation_haar", {"modulation", false, .82, true}},
    };
    const Def& d = defs.at(name);
    if (name == "stereo_side" && bank.sideFraction < 1e-7) return {};
    Mat x = bank.features.at(d.feature);
    if (name == "bass_contour") {
        Mat y(x.rows, x.cols * 2);
        for (int t = 0; t < x.rows; ++t)
            for (int c = 0; c < x.cols; ++c) {
                y.at(t, c) = x.at(t, c);
                y.at(t, x.cols + c) = x.at(t, c) - x.at(t > 0 ? t - 1 : 0, c);
            }
        x = std::move(y);
    }
    Mat p;
    std::vector<int> idx;
    patches(x, bank.dt, cfg.context, 2, 6, d.center, p, idx);
    double emax = 0;
    for (double e : bank.energy) emax = std::max(emax, e);
    const double floor = std::max(emax * 1e-7, kEps);
    for (int r = 0; r < p.rows; ++r)
        if (!(bank.energy[static_cast<std::size_t>(idx[static_cast<std::size_t>(r)])] > floor))
            std::fill(p.row(r), p.row(r) + p.cols, 0.0);
    return neighbors(p, idx, bank.dt, cfg, d.tree, d.threshold, cfg.context, name);
}

Seeds landmarkWorker(const Bank& bank, const Config& cfg) {
    Seeds out;
    out.null = .2;
    const Mat& m = bank.magnitude;
    const Mat local = maxFilter2d(m, 3, 7);
    std::vector<std::pair<int, int>> peaks;
    std::map<int, std::vector<int>> bytime;
    for (int t = 0; t < m.rows; ++t) {
        double mx = 0;
        for (int f = 0; f < m.cols; ++f) mx = std::max(mx, m.at(t, f));
        std::vector<int> ix;
        for (int f = 0; f < m.cols; ++f)
            if (m.at(t, f) >= local.at(t, f) && m.at(t, f) > mx * .16 && bank.frequency[static_cast<std::size_t>(f)] > 80) ix.push_back(f);
        std::stable_sort(ix.begin(), ix.end(), [&](int a, int b) { return m.at(t, a) < m.at(t, b); });
        for (std::size_t k = ix.size() > 5 ? ix.size() - 5 : 0; k < ix.size(); ++k) {
            peaks.push_back({t, ix[k]});
            bytime[t].push_back(ix[k]);
        }
    }
    std::map<std::tuple<int, int, int>, std::vector<int>> postings;
    const double gap = std::max(cfg.minSeparation, cfg.minRepeat), ctx = std::max(cfg.minRepeat, .8);
    for (const auto& [t, f] : peaks)
        for (int delta : {2, 4, 7}) {
            auto it = bytime.find(t + delta);
            if (it == bytime.end()) continue;
            for (int g : it->second) {
                std::vector<int>& post = postings[{f / 2, g / 2, delta}];
                for (int u : post)
                    if ((t - u) * bank.dt >= gap) out.seeds.push_back({u * bank.dt, t * bank.dt, .95, ctx});
                if (static_cast<int>(post.size()) < cfg.indexBucketCap) post.push_back(t);
                else post.back() = t;
            }
        }
    return out;
}

Seeds saxWorker(const Bank& bank, const Config& cfg) {
    Seeds out;
    out.null = .35;
    const Mat& x = bank.features.at("onset");
    const int half = std::max(3, static_cast<int>(cfg.context / bank.dt / 2));
    const int bins = 4, C = x.cols;
    std::vector<int> idx;
    for (int i = half; i < x.rows - half; i += 2) idx.push_back(i);
    if (idx.empty()) return out;
    const int N = static_cast<int>(idx.size());
    std::vector<double> p(static_cast<std::size_t>(N) * bins * C);
    std::vector<int> sym(p.size());
    auto P = [&](int n, int b, int c) -> double& { return p[(static_cast<std::size_t>(n) * bins + b) * C + c]; };
    for (int n = 0; n < N; ++n) {
        const int i = idx[static_cast<std::size_t>(n)], len = 2 * half;
        int start = i - half;
        for (int b = 0; b < bins; ++b) {
            const int size = len / bins + (b < len % bins ? 1 : 0);
            for (int c = 0; c < C; ++c) {
                double s = 0;
                for (int k = start; k < start + size; ++k) s += x.at(k, c);
                P(n, b, c) = s / size;
            }
            start += size;
        }
        for (int c = 0; c < C; ++c) {
            double m = 0;
            for (int b = 0; b < bins; ++b) m += P(n, b, c);
            m /= bins;
            double v = 0;
            for (int b = 0; b < bins; ++b) v += (P(n, b, c) - m) * (P(n, b, c) - m);
            const double sd = std::sqrt(v / bins);
            for (int b = 0; b < bins; ++b) P(n, b, c) = (P(n, b, c) - m) / (sd + 1e-8);
        }
    }
    for (std::size_t k = 0; k < p.size(); ++k) {
        int s = 0;
        for (double e : {-.67449, 0.0, .67449}) s += p[k] >= e ? 1 : 0;
        sym[k] = s;
    }
    std::map<std::pair<int, std::vector<int>>, std::vector<int>> posts;
    std::map<std::pair<int, int>, double> pairs;
    const double gap = std::max(cfg.minSeparation, cfg.context);
    for (int n = 0; n < N; ++n) {
        const int t = idx[static_cast<std::size_t>(n)];
        for (int c = 0; c < C; ++c) {
            double m = 0, v = 0;
            for (int b = 0; b < bins; ++b) m += P(n, b, c);
            m /= bins;
            for (int b = 0; b < bins; ++b) v += (P(n, b, c) - m) * (P(n, b, c) - m);
            if (std::sqrt(v / bins) < .1) continue;
            std::vector<int> word(static_cast<std::size_t>(bins));
            for (int b = 0; b < bins; ++b) word[static_cast<std::size_t>(b)] = sym[(static_cast<std::size_t>(n) * bins + b) * C + c];
            std::vector<int>& bucket = posts[{c, word}];
            for (int prev : bucket) {
                if ((t - idx[static_cast<std::size_t>(prev)]) * bank.dt < gap) continue;
                int same = 0;
                for (int k = 0; k < bins * C; ++k)
                    same += sym[static_cast<std::size_t>(n) * bins * C + k] == sym[static_cast<std::size_t>(prev) * bins * C + k];
                const double score = static_cast<double>(same) / (bins * C);
                if (score >= .64) {
                    auto key = std::make_pair(prev, n);
                    auto it = pairs.find(key);
                    pairs[key] = it == pairs.end() ? score : std::max(it->second, score);
                }
            }
            if (static_cast<int>(bucket.size()) < cfg.indexBucketCap) bucket.push_back(n);
            else bucket[static_cast<std::size_t>(n % cfg.indexBucketCap)] = n;
        }
    }
    for (const auto& [key, s] : pairs)
        out.seeds.push_back({idx[static_cast<std::size_t>(key.first)] * bank.dt, idx[static_cast<std::size_t>(key.second)] * bank.dt, s, cfg.context});
    return out;
}

Seeds autocorrWorker(const Bank& bank, const Config& cfg) {
    Seeds out;
    out.null = .4;
    const Mat& x0 = bank.features.at("onset");
    const int n = x0.rows;
    std::vector<double> ac(static_cast<std::size_t>(n), 0.0);
    for (int c = 0; c < x0.cols; ++c) {
        std::vector<double> col(static_cast<std::size_t>(n));
        double m = 0;
        for (int t = 0; t < n; ++t) m += x0.at(t, c);
        m /= std::max(1, n);
        for (int t = 0; t < n; ++t) col[static_cast<std::size_t>(t)] = x0.at(t, c) - m;
        std::vector<double> rev(col.rbegin(), col.rend());
        const std::vector<double> full = convolveFull(col, rev);
        for (int k = 0; k < n; ++k) ac[static_cast<std::size_t>(k)] += full[static_cast<std::size_t>(n - 1 + k)];
    }
    for (int k = 0; k < n; ++k) ac[static_cast<std::size_t>(k)] /= std::max(n - k, 1);
    const int lo = std::max(1, static_cast<int>(std::max(cfg.minSeparation, cfg.context) / bank.dt));
    if (lo >= n) return out;
    std::vector<double> tail(ac.begin() + lo, ac.end());
    std::vector<long long> peaks = findPeaksDistance(tail, std::max(2, static_cast<int>(.3 / bank.dt)));
    std::vector<int> lags;
    for (long long pk : peaks) lags.push_back(static_cast<int>(pk) + lo);
    std::stable_sort(lags.begin(), lags.end(), [&](int a, int b) { return ac[static_cast<std::size_t>(a)] < ac[static_cast<std::size_t>(b)]; });
    if (lags.size() > 32) lags.erase(lags.begin(), lags.end() - 32);
    Mat p;
    std::vector<int> idx;
    patches(bank.features.at("percussion"), bank.dt, cfg.context, 1, 6, true, p, idx);
    if (idx.empty()) return out;
    const int first = idx.front(), last = idx.back();
    for (int lag : lags) {
        int count = 0;
        for (int i : idx) {
            if (i + lag > last) break;
            if (count++ % 2) continue;
            const double s = rowDot(p, i - first, i + lag - first);
            if (s > .87) out.seeds.push_back({i * bank.dt, (i + lag) * bank.dt, s, cfg.context});
        }
    }
    return out;
}

Seeds massWorker(const Bank& bank, const Config& cfg) {
    Seeds out;
    const Mat& x = bank.features.at("envelope");
    const int m = std::max(6, static_cast<int>(std::nearbyint(cfg.context / bank.dt))), n = x.rows, C = x.cols;
    if (n < 2 * m) return out;
    const int L = n - m + 1;
    Mat means(L, C), sd(L, C);
    std::vector<std::vector<double>> cols(static_cast<std::size_t>(C), std::vector<double>(static_cast<std::size_t>(n)));
    for (int c = 0; c < C; ++c) {
        std::vector<double> cs(static_cast<std::size_t>(n + 1), 0.0), cs2(static_cast<std::size_t>(n + 1), 0.0);
        for (int t = 0; t < n; ++t) {
            cols[static_cast<std::size_t>(c)][static_cast<std::size_t>(t)] = x.at(t, c);
            cs[static_cast<std::size_t>(t + 1)] = cs[static_cast<std::size_t>(t)] + x.at(t, c);
            cs2[static_cast<std::size_t>(t + 1)] = cs2[static_cast<std::size_t>(t)] + x.at(t, c) * x.at(t, c);
        }
        for (int k = 0; k < L; ++k) {
            const double mu = (cs[static_cast<std::size_t>(k + m)] - cs[static_cast<std::size_t>(k)]) / m;
            means.at(k, c) = mu;
            sd.at(k, c) = std::sqrt(std::max((cs2[static_cast<std::size_t>(k + m)] - cs2[static_cast<std::size_t>(k)]) / m - mu * mu, 0.0));
        }
    }
    std::vector<int> anchors;
    const int Q = std::min(cfg.profileQueries, L);
    for (int k = 0; k < Q; ++k) {
        const int a = Q == 1 ? 0 : static_cast<int>(static_cast<double>(n - m) * k / (Q - 1));
        if (anchors.empty() || anchors.back() != a) anchors.push_back(a);
    }
    const int exclusion = std::max(m, static_cast<int>(cfg.minSeparation / bank.dt));
    std::vector<std::vector<Seed>> found(anchors.size());
    parallelFor(static_cast<long long>(anchors.size()), 4, [&](long long b, long long e) {
        for (long long ai = b; ai < e; ++ai) {
            const int q = anchors[static_cast<std::size_t>(ai)];
            std::vector<int> active;
            std::vector<double> qmean(static_cast<std::size_t>(C)), qsd(static_cast<std::size_t>(C));
            for (int c = 0; c < C; ++c) {
                double mu = 0, v = 0;
                for (int k = 0; k < m; ++k) mu += x.at(q + k, c);
                mu /= m;
                for (int k = 0; k < m; ++k) v += (x.at(q + k, c) - mu) * (x.at(q + k, c) - mu);
                qmean[static_cast<std::size_t>(c)] = mu;
                qsd[static_cast<std::size_t>(c)] = std::sqrt(v / m);
                if (qsd[static_cast<std::size_t>(c)] > 1e-4) active.push_back(c);
            }
            if (active.empty()) continue;
            std::vector<double> sim(static_cast<std::size_t>(L), 0.0);
            for (int c : active) {
                std::vector<double> query(cols[static_cast<std::size_t>(c)].begin() + q, cols[static_cast<std::size_t>(c)].begin() + q + m);
                const std::vector<double> dots = correlateValid(cols[static_cast<std::size_t>(c)], query);
                for (int k = 0; k < L; ++k) {
                    const double corr = (dots[static_cast<std::size_t>(k)] - m * means.at(k, c) * qmean[static_cast<std::size_t>(c)]) /
                                        (m * sd.at(k, c) * qsd[static_cast<std::size_t>(c)] + kEps);
                    sim[static_cast<std::size_t>(k)] += std::clamp(corr, -1.0, 1.0) / static_cast<double>(active.size());
                }
            }
            for (int k = std::max(0, q - exclusion); k < std::min(L, q + exclusion + 1); ++k) sim[static_cast<std::size_t>(k)] = -1;
            for (int rep = 0; rep < 3; ++rep) {
                const int j = static_cast<int>(std::max_element(sim.begin(), sim.end()) - sim.begin());
                const double s = sim[static_cast<std::size_t>(j)];
                if (s < .83) break;
                const int a = std::min(q, j), bb = std::max(q, j);
                found[static_cast<std::size_t>(ai)].push_back({(a + m / 2.0) * bank.dt, (bb + m / 2.0) * bank.dt, s, cfg.context});
                for (int k = std::max(0, j - m); k < std::min(L, j + m + 1); ++k) sim[static_cast<std::size_t>(k)] = -1;
            }
        }
    });
    for (auto& f : found) out.seeds.insert(out.seeds.end(), f.begin(), f.end());
    return out;
}

void dtwPath(const Mat& x, int a0, int b0, int len, int band, std::vector<int>& ia, std::vector<int>& ib, double& d) {
    const int n = len, m = len;
    std::vector<double> cost(static_cast<std::size_t>(n + 1) * (m + 1), 1e20);
    std::vector<signed char> ptr(cost.size(), 0);
    auto C = [&](int i, int j) -> double& { return cost[static_cast<std::size_t>(i) * (m + 1) + j]; };
    auto P = [&](int i, int j) -> signed char& { return ptr[static_cast<std::size_t>(i) * (m + 1) + j]; };
    C(0, 0) = 0;
    for (int i = 1; i <= n; ++i)
        for (int j = std::max(1, i - band); j <= std::min(m, i + band); ++j) {
            const double dd = 1.0 - rowDot(x, a0 + i - 1, b0 + j - 1);
            const double v0 = C(i - 1, j - 1), v1 = C(i - 1, j) + .08, v2 = C(i, j - 1) + .08;
            if (v0 <= v1 && v0 <= v2) { C(i, j) = v0 + dd; P(i, j) = 0; }
            else if (v1 <= v2) { C(i, j) = v1 + dd; P(i, j) = 1; }
            else { C(i, j) = v2 + dd; P(i, j) = 2; }
        }
    ia.clear();
    ib.clear();
    int i = n, j = m;
    while (i > 0 && j > 0) {
        ia.push_back(i - 1);
        ib.push_back(j - 1);
        const int p = P(i, j);
        if (p == 0) { --i; --j; }
        else if (p == 1) --i;
        else --j;
    }
    d = C(n, m) / std::max<std::size_t>(1, ia.size());
}

Seeds dtwWorker(const Bank& bank, const Config& cfg) {
    Mat cens = bank.features.at("cens"), spec = bank.features.at("spectral");
    rowsUnit(cens, false);
    rowsUnit(spec, true);
    Mat x(cens.rows, cens.cols + spec.cols);
    for (int t = 0; t < x.rows; ++t) {
        for (int c = 0; c < cens.cols; ++c) x.at(t, c) = cens.at(t, c);
        for (int c = 0; c < spec.cols; ++c) x.at(t, cens.cols + c) = .5 * spec.at(t, c);
    }
    rowsUnit(x, false);
    Mat p;
    std::vector<int> idx;
    patches(x, bank.dt, cfg.context, 2, 4, false, p, idx);
    Seeds coarse = neighbors(p, idx, bank.dt, cfg, true, .80, cfg.context, "dtw_seeds");
    Seeds out;
    out.null = coarse.null;
    const int radius = std::max(6, static_cast<int>(std::max(2.0, cfg.context) / bank.dt / 2));
    std::stable_sort(coarse.seeds.begin(), coarse.seeds.end(), [](const Seed& a, const Seed& b) { return a.score > b.score; });
    std::set<std::pair<int, int>> seen;
    int count = 0;
    std::vector<int> ia, ib;
    for (const Seed& s : coarse.seeds) {
        const auto key = std::make_pair(static_cast<int>(s.source), static_cast<int>(s.target));
        if (seen.count(key)) continue;
        seen.insert(key);
        const int a = static_cast<int>(std::nearbyint(s.source / bank.dt)), b = static_cast<int>(std::nearbyint(s.target / bank.dt));
        if (a < radius || b + radius >= x.rows) continue;
        double d;
        dtwPath(x, a - radius, b - radius, 2 * radius, std::max(2, radius / 4), ia, ib, d);
        if (d < .18 && static_cast<int>(ia.size()) > radius) {
            double slope = 1.0;
            std::vector<double> fb(ib.begin(), ib.end()), fa(ia.begin(), ia.end());
            if (stdev(fb) > 0) {
                const double mb = mean(fb), ma = mean(fa);
                double sxy = 0, sxx = 0;
                for (std::size_t k = 0; k < fb.size(); ++k) {
                    sxy += (fb[k] - mb) * (fa[k] - ma);
                    sxx += (fb[k] - mb) * (fb[k] - mb);
                }
                slope = sxy / sxx;
            }
            if (slope > .9 && slope < 1.1) out.seeds.push_back({s.source, s.target, 1 - d, 2 * radius * bank.dt, slope});
        }
        if (++count >= 100) break;
    }
    return out;
}

std::vector<Proposal> toProposals(std::vector<Seed>& seeds, const std::string& name, double null, const Bank& bank, const Config& cfg) {
    const double quantum = std::max(bank.dt * 2, .08), gap = std::max(cfg.minSeparation, cfg.minRepeat);
    std::vector<long long> order;
    std::map<long long, std::vector<Seed>> grouped;
    for (const Seed& s : seeds) {
        if (s.target - s.source < gap) continue;
        const long long key = static_cast<long long>(std::nearbyint((s.target - s.source) / quantum));
        auto it = grouped.find(key);
        if (it == grouped.end()) order.push_back(key);
        grouped[key].push_back(s);
    }
    std::vector<Proposal> props;
    for (long long key : order) {
        std::vector<Seed>& group = grouped[key];
        std::stable_sort(group.begin(), group.end(), [](const Seed& a, const Seed& b) { return a.source < b.source; });
        std::vector<std::vector<Seed>> runs;
        std::vector<Seed> run;
        for (const Seed& s : group) {
            if (!run.empty() && s.source - run.back().source > std::max(s.context, run.back().context) * 1.15) {
                runs.push_back(run);
                run.clear();
            }
            run.push_back(s);
        }
        if (!run.empty()) runs.push_back(run);
        for (const auto& r : runs) {
            std::vector<double> offs, rates, scores;
            double start = 1e300, end = -1e300;
            for (const Seed& s : r) {
                offs.push_back(s.target - s.source);
                rates.push_back(s.rate);
                scores.push_back(s.score);
                start = std::min(start, s.source - s.context / 2);
                end = std::max(end, s.source + s.context / 2);
            }
            const double offset = median(offs);
            start = std::max(0.0, start);
            end = std::min(bank.duration - offset, end);
            const double maxlen = std::min(cfg.maxRepeat, offset - .025);
            if (maxlen < cfg.minRepeat) continue;
            std::sort(scores.begin(), scores.end(), std::greater<double>());
            const std::size_t top = std::max<std::size_t>(1, r.size() / 2);
            const double score = mean(std::vector<double>(scores.begin(), scores.begin() + static_cast<std::ptrdiff_t>(top)));
            const double rate = median(rates);
            double pos = start;
            while (end - pos >= cfg.minRepeat - .001) {
                const double dur = std::min(end - pos, maxlen);
                props.push_back({pos, pos + offset, dur, name, score, null, static_cast<int>(r.size()), rate});
                pos += maxlen;
            }
        }
    }
    std::stable_sort(props.begin(), props.end(), [](const Proposal& a, const Proposal& b) {
        auto k = [](const Proposal& p) { return (p.score - p.nullMedian) * std::sqrt(p.duration) * std::log2(2.0 + p.support); };
        return k(a) > k(b);
    });
    if (static_cast<int>(props.size()) > cfg.perWorkerLimit) props.resize(static_cast<std::size_t>(cfg.perWorkerLimit));
    return props;
}

}

std::vector<Proposal> runWorker(const std::string& name, const Audio& audio, int sr, const Bank& bank, const Config& cfg) {
    Seeds s;
    if (name == "pcm_hash") s = pcmWorker(audio, sr, cfg);
    else if (name == "landmark_pairs") s = landmarkWorker(bank, cfg);
    else if (name == "onset_sax") s = saxWorker(bank, cfg);
    else if (name == "autocorr_lags") s = autocorrWorker(bank, cfg);
    else if (name == "mass_profile") s = massWorker(bank, cfg);
    else if (name == "subsequence_dtw") s = dtwWorker(bank, cfg);
    else s = featureWorker(bank, cfg, name);
    return toProposals(s.seeds, name, s.null, bank, cfg);
}

}
}
