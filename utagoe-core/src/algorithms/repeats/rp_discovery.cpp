#include "rp_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <numeric>
#include <set>

namespace utagoe {
namespace rp {
namespace {

const std::pair<std::string, double>& infoOf(const std::string& name) {
    for (const auto& w : workerInfo())
        if (w.first == name) return w.second;
    throw Error("unknown worker: " + name);
}

std::map<std::string, double> evidenceWeights(const std::map<std::string, double>& scores, const std::map<std::string, double>& nulls) {
    std::map<std::string, std::map<std::string, double>> family;
    for (const auto& [name, s] : scores) {
        const auto& info = infoOf(name);
        auto it = nulls.find(name);
        const double nul = it == nulls.end() ? 0.0 : it->second;
        const double contrast = std::clamp((s - nul) / (1.0 - nul + 1e-9), .01, 1.0);
        family[info.first][name] = info.second * contrast;
    }
    std::map<std::string, double> mass;
    double total = 0;
    for (const auto& [f, v] : family) {
        double m = 0;
        for (const auto& kv : v) m = std::max(m, kv.second);
        mass[f] = m;
        total += m;
    }
    if (total == 0) total = 1.0;
    std::map<std::string, double> out;
    for (const auto& [f, v] : family) {
        double sub = 0;
        for (const auto& kv : v) sub += kv.second;
        for (const auto& kv : v) out[kv.first] = (kv.second / sub) * (mass[f] / total);
    }
    return out;
}

struct Group {
    double start, end, offset;
    std::vector<double> offsets;
    std::vector<const Proposal*> members;
};

std::string matchId(int k) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "match_%04d", k);
    return buf;
}

std::vector<Match> mergeProposals(std::vector<Proposal> proposals, double duration, const Config& cfg, int maxMatches) {
    std::stable_sort(proposals.begin(), proposals.end(), [](const Proposal& a, const Proposal& b) {
        return a.score * std::sqrt(a.duration) > b.score * std::sqrt(b.duration);
    });
    std::vector<Group> groups;
    for (const Proposal& p : proposals) {
        const double off = p.target - p.source;
        Group* found = nullptr;
        for (Group& g : groups) {
            if (std::abs(g.offset - off) > .30) continue;
            const double overlap = std::max(0.0, std::min(g.end, p.source + p.duration) - std::max(g.start, p.source));
            if (overlap / std::max(.001, std::min(g.end - g.start, p.duration)) >= .45) {
                if (std::max(g.end, p.source + p.duration) - std::min(g.start, p.source) > cfg.maxRepeat) continue;
                found = &g;
                break;
            }
        }
        if (!found) {
            groups.push_back({p.source, p.source + p.duration, off, {off}, {&p}});
        } else {
            found->start = std::min(found->start, p.source);
            found->end = std::max(found->end, p.source + p.duration);
            found->offsets.push_back(off);
            found->offset = median(found->offsets);
            found->members.push_back(&p);
        }
    }
    std::vector<Match> matches;
    for (const Group& g : groups) {
        double start = g.start;
        const double limit = std::min(g.offset - .025, cfg.maxRepeat);
        if (limit < cfg.minRepeat) continue;
        while (start < g.end) {
            const double end = std::min({g.end, start + limit, duration - g.offset});
            const double d = end - start;
            if (d < cfg.minRepeat) break;
            std::vector<const Proposal*> members;
            for (const Proposal* p : g.members)
                if (std::min(end, p->source + p->duration) - std::max(start, p->source) > std::min(.4, d * .25)) members.push_back(p);
            if (members.empty()) {
                start = end;
                continue;
            }
            std::map<std::string, double> scores, nulls;
            std::vector<double> rates;
            for (const Proposal* p : members) {
                auto it = scores.find(p->worker);
                scores[p->worker] = std::max(it == scores.end() ? 0.0 : it->second, std::min(1.0, p->score));
                nulls[p->worker] = p->nullMedian;
                rates.push_back(p->rate);
            }
            const auto weights = evidenceWeights(scores, nulls);
            double score = 0;
            for (const auto& [n, w] : weights) score += w * scores[n];
            Match m;
            m.source = start;
            m.target = start + g.offset;
            m.duration = d;
            m.scores.assign(scores.begin(), scores.end());
            m.weights.assign(weights.begin(), weights.end());
            m.discoveryScore = score;
            m.rate = median(rates);
            matches.push_back(m);
            start = end;
        }
    }
    auto key = [](const Match& m) {
        std::set<std::string> fams;
        for (const auto& s : m.scores) fams.insert(infoOf(s.first).first);
        return m.discoveryScore * std::sqrt(m.duration) * (1 + .15 * static_cast<double>(fams.size()));
    };
    std::vector<double> keys;
    for (const Match& m : matches) keys.push_back(key(m));
    std::vector<std::size_t> order(matches.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return keys[a] > keys[b]; });
    std::vector<Match> selected;
    for (std::size_t oi : order) {
        const Match& m = matches[oi];
        bool duplicate = false;
        for (const Match& q : selected)
            if (std::abs((m.target - m.source) - (q.target - q.source)) < .35) {
                const double ov = std::max(0.0, std::min(m.source + m.duration, q.source + q.duration) - std::max(m.source, q.source));
                if (ov / std::max(.01, std::min(m.duration, q.duration)) > .8) {
                    duplicate = true;
                    break;
                }
            }
        if (!duplicate) selected.push_back(m);
        if (static_cast<int>(selected.size()) >= maxMatches) break;
    }
    for (std::size_t k = 0; k < selected.size(); ++k) selected[k].id = matchId(static_cast<int>(k) + 1);
    return selected;
}

std::vector<Match> preflightRank(const Audio& audio, int sr, std::vector<Match> matches, const Config& cfg) {
    const int rate = std::min(sr, 4000), g = std::gcd(sr, rate);
    Audio x;
    x.channels = audio.channels;
    {
        std::vector<std::vector<double>> ch(static_cast<std::size_t>(audio.channels));
        for (int c = 0; c < audio.channels; ++c) {
            std::vector<double> v(audio.frames());
            for (std::size_t i = 0; i < v.size(); ++i) v[i] = static_cast<float>(audio.at(i, c));
            ch[static_cast<std::size_t>(c)] = resamplePoly(v, rate / g, sr / g);
        }
        x.v.resize(ch[0].size() * static_cast<std::size_t>(x.channels));
        for (std::size_t i = 0; i < ch[0].size(); ++i)
            for (int c = 0; c < x.channels; ++c) x.at(i, c) = static_cast<float>(ch[static_cast<std::size_t>(c)][i]);
    }
    parallelFor(static_cast<long long>(matches.size()), 1, [&](long long b, long long e) {
        for (long long mi = b; mi < e; ++mi) {
            Match& m = matches[static_cast<std::size_t>(mi)];
            const double window = std::min(.30, std::max(.12, m.duration / 6));
            const int n = static_cast<int>(window * rate), margin = static_cast<int>(cfg.maxLag * rate);
            const int count = std::min(7, std::max(3, static_cast<int>(m.duration / 2)));
            const double c0 = window / 2 + .03, c1 = m.duration - window / 2 - .03;
            std::map<std::string, std::vector<double>> obs;
            const std::vector<std::string> modes = x.channels == 2 ? std::vector<std::string>{"full", "side"} : std::vector<std::string>{"full"};
            for (int k = 0; k < count; ++k) {
                const double c = count == 1 ? c0 : c0 + (c1 - c0) * k / (count - 1);
                const long long si = static_cast<long long>(std::nearbyint((m.source + c - window / 2) * rate));
                const long long ti = static_cast<long long>(std::nearbyint((m.target + c - window / 2) * rate));
                const Audio r = slicePad(x, si - margin, n + 2 * margin), y = slicePad(x, ti, n);
                for (const std::string& mode : modes) {
                    const Audio rg = guideAudio(r, mode), yg = guideAudio(y, mode);
                    double ey = 0, er = 0;
                    for (double v : yg.v) ey += v * v;
                    for (double v : rg.v) er += v * v;
                    if (ey < 1e-12 || er < 1e-12) {
                        obs[mode].push_back(0.0);
                        continue;
                    }
                    double lag, q;
                    corrPeak(rg, yg, lag, q);
                    obs[mode].push_back(q);
                }
            }
            std::map<std::string, double> score;
            for (auto& [mode, qs] : obs) {
                std::sort(qs.begin(), qs.end(), std::greater<double>());
                score[mode] = mean(std::vector<double>(qs.begin(), qs.begin() + std::min<std::ptrdiff_t>(2, static_cast<std::ptrdiff_t>(qs.size()))));
            }
            std::string chosen = "full";
            double best = score.count("full") ? score["full"] : -1;
            if (score.count("side") && score["side"] > best) chosen = "side";
            if (cfg.guide == "side" || cfg.guide == "full") chosen = cfg.guide;
            m.preflightScore = score.count(chosen) ? score[chosen] : 0.0;
            m.preferredGuide = chosen;
        }
    });
    auto rank = [](const Match& m) {
        const double q = m.preflightScore;
        return q * q * (.75 + .25 * m.discoveryScore) * (1 + .06 * std::log1p(m.duration));
    };
    std::vector<std::size_t> ordered(matches.size());
    std::iota(ordered.begin(), ordered.end(), 0);
    std::stable_sort(ordered.begin(), ordered.end(), [&](std::size_t a, std::size_t b) { return rank(matches[a]) > rank(matches[b]); });
    std::vector<std::vector<std::size_t>> bins(3);
    for (std::size_t i : ordered) {
        const double d = matches[i].duration;
        bins[d <= 4 ? 0 : (d <= 12 ? 1 : 2)].push_back(i);
    }
    const std::size_t quota = static_cast<std::size_t>(std::max(1, cfg.maxMatches / 4));
    std::vector<std::size_t> chosen;
    std::set<std::size_t> seen;
    for (const auto& bucket : bins)
        for (std::size_t k = 0; k < std::min(quota, bucket.size()); ++k) {
            chosen.push_back(bucket[k]);
            seen.insert(bucket[k]);
        }
    for (std::size_t i : ordered) {
        if (!seen.count(i)) {
            chosen.push_back(i);
            seen.insert(i);
        }
        if (static_cast<int>(chosen.size()) >= cfg.maxMatches) break;
    }
    if (static_cast<int>(chosen.size()) > cfg.maxMatches) chosen.resize(static_cast<std::size_t>(cfg.maxMatches));
    std::stable_sort(chosen.begin(), chosen.end(), [&](std::size_t a, std::size_t b) { return rank(matches[a]) > rank(matches[b]); });
    std::vector<Match> out;
    for (std::size_t k = 0; k < chosen.size(); ++k) {
        out.push_back(matches[chosen[k]]);
        out.back().id = matchId(static_cast<int>(k) + 1);
    }
    return out;
}

}

std::vector<Match> discover(const Audio& audio, int sr, const Config& cfg, const Progress& progress, bool& cancelled) {
    cancelled = false;
    if (progress && !progress(0.0, "features")) {
        cancelled = true;
        return {};
    }
    const Bank bank = buildBank(audio, sr, cfg);
    const auto& info = workerInfo();
    std::vector<std::vector<Proposal>> perWorker(info.size());
    parallelFor(static_cast<long long>(info.size()), 1, [&](long long b, long long e) {
        for (long long w = b; w < e; ++w) {
            perWorker[static_cast<std::size_t>(w)] = runWorker(info[static_cast<std::size_t>(w)].first, audio, sr, bank, cfg);
        }
    });
    if (progress && !progress(.6, "merge")) {
        cancelled = true;
        return {};
    }
    std::vector<Proposal> proposals;
    for (auto& p : perWorker) proposals.insert(proposals.end(), p.begin(), p.end());
    const double duration = static_cast<double>(audio.frames()) / sr;
    std::vector<Match> matches = mergeProposals(proposals, duration, cfg, cfg.preflight ? cfg.maxMatches * 4 : cfg.maxMatches);
    if (cfg.preflight) matches = preflightRank(audio, sr, std::move(matches), cfg);
    return matches;
}

}
}
