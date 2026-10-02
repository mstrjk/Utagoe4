#ifndef UTAGOE_FW_H
#define UTAGOE_FW_H

#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace utagoe {
namespace fw {

struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct Config {
    int analysisRate = 8000;
    double featureStep = 0.10;
    double context = 1.6;
    double minRepeat = 1.2;
    double maxRepeat = 32.0;
    double minSeparation = 1.2;
    int maxMatches = 120;
    int perWorkerLimit = 180;
    int indexBucketCap = 32;
    int profileQueries = 96;
    unsigned long long seed = 1729;
    std::string guide = "auto";
    double maxLag = 0.24;
    double maxRatePpm = 12000.0;
    double entropy = 0.02;
    double minValidationDb = 0.5;
    double minCorrelation = 0.25;
    int firTaps = 33;
    int renderLimit = 24;
    bool multireference = true;
    bool refineSubregions = true;
    bool preflight = true;
};

struct Audio {
    int channels = 1;
    std::vector<double> v;
    std::size_t frames() const { return channels > 0 ? v.size() / static_cast<std::size_t>(channels) : 0; }
    double& at(std::size_t i, int c) { return v[i * static_cast<std::size_t>(channels) + static_cast<std::size_t>(c)]; }
    double at(std::size_t i, int c) const { return v[i * static_cast<std::size_t>(channels) + static_cast<std::size_t>(c)]; }
};

struct Seed {
    double source, target, score, context, rate = 1.0;
};

struct Proposal {
    double source, target, duration;
    std::string worker;
    double score, nullMedian = 0.0;
    int support = 1;
    double rate = 1.0;
};

struct Match {
    std::string id;
    double source = 0, target = 0, duration = 0;
    std::vector<std::pair<std::string, double>> scores;
    std::vector<std::pair<std::string, double>> weights;
    double discoveryScore = 0, rate = 1.0;
    std::string status = "unverified";
    std::string preferredGuide;
    double preflightScore = 0;
};

struct Metrics {
    double reductionDb = 0, correlation = 0, residualRatio = 0;
    long long observations = 0;
};

struct Span {
    double start, end, value;
};

struct Extraction {
    Audio target, aligned, shared, residual;
    std::string status, quality, guide;
    Metrics heldOut, full;
    double delaySamples = 0, ratePpm = 0, correlationAdvantage = 0;
    std::vector<std::pair<std::string, double>> audioWeights;
    std::vector<Span> subregions;
};

struct JointResult {
    std::string name;
    std::vector<std::string> sources;
    double targetStart = 0, duration = 0;
    bool accepted = false;
    double advantageDb = 0;
    Metrics heldOut;
    Audio shared, residual;
};

struct Outcome {
    std::vector<Match> matches;
    std::map<std::string, Extraction> results;
    std::vector<std::string> errors;
    std::vector<JointResult> joints;
    bool cancelled = false;
};

using Progress = std::function<bool(double, const std::string&)>;
using ResultSink = std::function<void(const Match&, Extraction&)>;
using JointSink = std::function<void(JointResult&)>;

std::vector<double> resamplePoly(const std::vector<double>& x, int up, int down);
std::vector<Match> discover(const Audio& audio, int sr, const Config& cfg, const Progress& progress, bool& cancelled);
Extraction extractMatch(const Audio& audio, int sr, const Match& m, const Config& cfg);
JointResult combineReferences(const Audio& target, const std::vector<Audio>& refs, int sr, const std::string& guide, const Config& cfg);
Outcome run(const Audio& audio, int sr, const Config& cfg, const Progress& progress, const ResultSink& onResult = nullptr,
            const JointSink& onJoint = nullptr);
std::vector<double> convexWeights(const std::vector<double>& y, const std::vector<std::vector<double>>& p, double entropy);

}
}

#endif
