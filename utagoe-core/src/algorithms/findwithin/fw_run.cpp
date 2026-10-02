#include "fw_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace utagoe {
namespace fw {

namespace {

void release(Audio& a) {
    std::vector<double>().swap(a.v);
}

}

Outcome run(const Audio& audio, int sr, const Config& cfg, const Progress& progress, const ResultSink& onResult, const JointSink& onJoint) {
    Outcome out;
    bool cancelled = false;
    std::vector<Match> scan = discover(audio, sr, cfg, [&](double f, const std::string& s) { return !progress || progress(f * .5, s); }, cancelled);
    if (cancelled) {
        out.cancelled = true;
        return out;
    }
    std::vector<Match> queue;
    for (const Match& m : scan) {
        if (static_cast<int>(queue.size()) >= cfg.renderLimit) break;
        queue.push_back(m);
    }
    std::set<std::string> queued;
    for (const Match& m : queue) queued.insert(m.id);
    std::vector<Match> verified;
    for (std::size_t qi = 0; qi < queue.size(); ++qi) {
        if (progress && !progress(.5 + .4 * static_cast<double>(qi) / static_cast<double>(queue.size()), "verify")) {
            out.cancelled = true;
            return out;
        }
        Match m = queue[qi];
        try {
            Extraction x = extractMatch(audio, sr, m, cfg);
            m.status = x.status;
            if (onResult) onResult(m, x);
            release(x.target);
            release(x.aligned);
            release(x.shared);
            release(x.residual);
            out.results[m.id] = std::move(x);
        } catch (const std::exception& e) {
            m.status = "verification_error";
            out.errors.push_back(m.id + ": " + e.what());
        }
        queue[qi].status = m.status;
        if (m.status == "validated_contrast" || m.status == "near_null_repeat") verified.push_back(m);
        const bool part = m.id.find("_part") != std::string::npos;
        if (cfg.refineSubregions && !part && (m.status == "musical_similarity_only" || m.status == "validated_contrast")) {
            std::vector<Span> spans = out.results[m.id].subregions;
            std::stable_sort(spans.begin(), spans.end(), [](const Span& a, const Span& b) { return a.end - a.start > b.end - b.start; });
            if (spans.size() > 3) spans.resize(3);
            for (std::size_t k = 0; k < spans.size(); ++k) {
                const Span& sp = spans[k];
                if (sp.end - sp.start > m.duration * .95) continue;
                Match child = m;
                char suffix[16];
                std::snprintf(suffix, sizeof suffix, "_part%02d", static_cast<int>(k) + 1);
                child.id = m.id + suffix;
                child.source = m.source + sp.start;
                child.target = m.target + sp.start;
                child.duration = sp.end - sp.start;
                child.status = "unverified";
                if (!queued.count(child.id)) {
                    queue.push_back(child);
                    queued.insert(child.id);
                }
            }
        }
    }
    out.matches = queue;
    if (cfg.multireference) {
        std::set<std::vector<std::string>> used;
        for (std::size_t i = 0; i < verified.size(); ++i) {
            const Match& m = verified[i];
            std::vector<const Match*> group = {&m};
            for (std::size_t j = i + 1; j < verified.size(); ++j) {
                const Match& q = verified[j];
                if (std::abs(q.source - m.source) < .4) continue;
                const double overlap = std::min(m.target + m.duration, q.target + q.duration) - std::max(m.target, q.target);
                if (overlap >= std::max(cfg.minRepeat, std::min(m.duration, q.duration) * .5)) group.push_back(&q);
                if (group.size() >= 4) break;
            }
            if (group.size() < 2) continue;
            std::vector<std::string> key;
            for (const Match* g : group) key.push_back(g->id);
            std::sort(key.begin(), key.end());
            if (used.count(key)) continue;
            used.insert(key);
            double start = -1e300, end = 1e300;
            for (const Match* g : group) {
                start = std::max(start, g->target);
                end = std::min(end, g->target + g->duration);
            }
            if (end - start < cfg.minRepeat) continue;
            std::string name = "joint";
            for (const Match* g : group) name += "_" + g->id.substr(6);
            if (progress && !progress(.9, "joint")) {
                out.cancelled = true;
                return out;
            }
            try {
                std::vector<Audio> refs;
                Audio target;
                std::string mode;
                for (const Match* g : group) {
                    Match cropped = *g;
                    cropped.source = g->source + (start - g->target);
                    cropped.target = start;
                    cropped.duration = end - start;
                    Audio r, y;
                    std::vector<char> valid;
                    std::string guide;
                    double delay, rate;
                    alignRegion(audio, sr, cropped, cfg, r, y, valid, guide, delay, rate);
                    long long nv = 0;
                    for (char c : valid) nv += c ? 1 : 0;
                    if (static_cast<double>(nv) < .9 * static_cast<double>(valid.size())) throw Error("insufficient safe joint reference support");
                    refs.push_back(std::move(r));
                    target = std::move(y);
                    if (mode.empty()) mode = guide;
                }
                JointResult jr = combineReferences(target, refs, sr, mode, cfg);
                jr.name = name;
                for (const Match* g : group) jr.sources.push_back(g->id);
                jr.targetStart = start;
                jr.duration = end - start;
                if (onJoint) onJoint(jr);
                release(jr.shared);
                release(jr.residual);
                out.joints.push_back(std::move(jr));
            } catch (const std::exception& e) {
                out.errors.push_back(name + ": " + e.what());
            }
            if (out.joints.size() >= 12) break;
        }
    }
    if (progress) progress(1.0, "done");
    return out;
}

}
}
