#include "pipe_config_gen.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>

#include <algorithm>
#include <numeric>
#include <random>
#include <stdexcept>
#include <map>

// Maps "pipeline.*" param-range key → (step_name, param_name_in_step_json)
static const std::map<std::string, std::pair<std::string,std::string>> PARAM_MAP = {
    { "pipeline.binary_threshold",    { "BinaryThreshold_CUDA",     "threshold"    } },
    { "pipeline.morph_ksize",         { "MorphologyClose_CUDA",     "ksize"        } },
    { "pipeline.contour_min_area",    { "ContourAreaFilter_CUDA",   "min_area"     } },
    { "pipeline.contour_max_contours",{ "ContourAreaFilter_CUDA",   "max_contours" } },
    { "pipeline.bg_history",          { "BackgroundSubtraction_CUDA","history"       } },
    { "pipeline.bg_var_threshold",    { "BackgroundSubtraction_CUDA","varThreshold"  } },  // camelCase matches JSON loader
    { "pipeline.bg_detect_shadows",   { "BackgroundSubtraction_CUDA","detectShadows" } },
    { "pipeline.gaussian_kernel_size",{ "GaussianBlur_CUDA",         "kernel_size"   } },
    { "pipeline.gaussian_sigma_x",    { "GaussianBlur_CUDA",         "sigma_x"       } },
};

// ── default base pipeline ─────────────────────────────────────────────────────

std::string PipelineConfigGen::defaultBasePipelineJson() {
    QJsonObject params_bs;
    params_bs["detectShadows"] = 0.0;
    params_bs["history"]       = 500.0;
    params_bs["varThreshold"]  = 16.0;

    QJsonObject params_bt;
    params_bt["max_value"] = 255.0;
    params_bt["threshold"] = 80.0;

    QJsonObject params_mc;
    params_mc["iterations"] = 2.0;
    params_mc["ksize"]      = 5.0;   // sweep-2 winner; override via --base-pipeline for different default
    params_mc["shape"]      = 2.0;

    QJsonObject params_ed;
    params_ed["aperture_size"] = 3.0;
    params_ed["threshold1"]    = 150.0;
    params_ed["threshold2"]    = 200.0;

    QJsonObject params_cf;
    params_cf["debug"]        = 1.0;
    params_cf["max_area"]     = 300000.0;
    params_cf["min_area"]     = 15.0;  // sweep-2 elbow point
    params_cf["max_contours"] = 1.0;   // always 1 — centroid smear guard

    auto step = [](const QString& name, const QJsonObject& p) {
        return QJsonObject{{"name",name},{"enabled",1},{"parameters",p}};
    };

    QJsonArray steps;
    steps.append(step("BackgroundSubtraction_CUDA", params_bs));
    steps.append(step("BinaryThreshold_CUDA",        params_bt));
    steps.append(step("MorphologyClose_CUDA",         params_mc));
    steps.append(step("EdgeDetection_CUDA",           params_ed));
    steps.append(step("ContourAreaFilter_CUDA",       params_cf));
    steps.append(step("MassCenterOverlay_CPU",        QJsonObject{}));

    QJsonObject pipe;
    pipe["processing_mode"] = "CUDA_PREFERRED";
    pipe["steps"] = steps;

    QJsonObject root;
    root["pipelineUID"]  = "pipe-base-default";
    root["status"]       = "active";
    root["description"]  = "base pipeline for pipeoptimizer sweep";
    root["pipeline"]     = pipe;

    return QString(QJsonDocument(root).toJson(QJsonDocument::Compact)).toStdString();
}

// ── Latin Hypercube Sampling ──────────────────────────────────────────────────

// Given N configs and M params with discrete value sets, produce N configs where
// each parameter's N slot assignments are a random permutation of {v[i % len(v)}}.
// This ensures uniform marginal coverage of each parameter's range.

std::vector<PipelineConfigGen::GeneratedConfig> PipelineConfigGen::generate(
    const std::string& base_pipeline_json,
    const std::string& param_ranges_path,
    int n,
    uint64_t seed)
{
    if (n <= 0) return {};

    // ── Load base pipeline ────────────────────────────────────────────────
    QJsonDocument baseDoc = QJsonDocument::fromJson(
        QByteArray::fromStdString(base_pipeline_json));
    if (baseDoc.isNull())
        throw std::runtime_error("pipe_config_gen: invalid base pipeline JSON");

    // ── Load param ranges ─────────────────────────────────────────────────
    QFile rangesFile(QString::fromStdString(param_ranges_path));
    if (!rangesFile.open(QIODevice::ReadOnly))
        throw std::runtime_error("pipe_config_gen: cannot open " + param_ranges_path);
    QJsonDocument rangesDoc = QJsonDocument::fromJson(rangesFile.readAll());
    rangesFile.close();
    if (rangesDoc.isNull())
        throw std::runtime_error("pipe_config_gen: invalid param_ranges.json");

    const QJsonObject rangesObj = rangesDoc.object();

    // Collect only "pipeline.*" entries that have a known step/param mapping
    struct PipeParam {
        std::string step_name;
        std::string param_name;
        std::vector<double> values;
    };
    std::vector<PipeParam> params;

    for (auto it = rangesObj.constBegin(); it != rangesObj.constEnd(); ++it) {
        const std::string key = it.key().toStdString();
        auto mapIt = PARAM_MAP.find(key);
        if (mapIt == PARAM_MAP.end()) continue;

        QJsonArray vals = it.value().toObject()["values"].toArray();
        if (vals.isEmpty()) continue;

        PipeParam pp;
        pp.step_name  = mapIt->second.first;
        pp.param_name = mapIt->second.second;
        for (const auto& v : vals) pp.values.push_back(v.toDouble());
        params.push_back(std::move(pp));
    }

    if (params.empty())
        throw std::runtime_error("pipe_config_gen: no pipeline.* params found in ranges file");

    // ── Build LHS slot-assignments ────────────────────────────────────────
    // lhs[param_idx][config_idx] = which value index that config uses for that param.
    // Each row is a permutation of {0..n-1 mod n_values}, giving uniform marginal coverage.
    std::mt19937_64 rng(seed);
    const int n_params = static_cast<int>(params.size());
    std::vector<std::vector<int>> lhs;
    lhs.resize(static_cast<size_t>(n_params));
    for (int par = 0; par < n_params; ++par) {
        const int nv = static_cast<int>(params[static_cast<size_t>(par)].values.size());
        lhs[static_cast<size_t>(par)].resize(static_cast<size_t>(n));
        for (int cfg = 0; cfg < n; ++cfg)
            lhs[static_cast<size_t>(par)][static_cast<size_t>(cfg)] = cfg % nv;
        std::shuffle(lhs[static_cast<size_t>(par)].begin(),
                     lhs[static_cast<size_t>(par)].end(), rng);
    }

    // ── Generate configs ──────────────────────────────────────────────────
    std::vector<GeneratedConfig> result;
    result.reserve(n);

    for (int ci = 0; ci < n; ++ci) {
        QJsonObject root = baseDoc.object();
        QJsonObject pipe = root["pipeline"].toObject();
        QJsonArray  steps = pipe["steps"].toArray();

        // Apply each param override
        for (int par = 0; par < n_params; ++par) {
            const size_t par_idx = static_cast<size_t>(par);
            const size_t ci_idx  = static_cast<size_t>(ci);
            const int    val_idx = lhs[par_idx][ci_idx];
            const double val     = params[par_idx].values[static_cast<size_t>(val_idx)];
            const QString sname   = QString::fromStdString(params[par_idx].step_name);
            const QString pname   = QString::fromStdString(params[par_idx].param_name);

            for (int si = 0; si < steps.size(); ++si) {
                QJsonObject step = steps[si].toObject();
                if (step["name"].toString() != sname) continue;
                QJsonObject stepParams = step["parameters"].toObject();
                stepParams[pname] = val;
                step["parameters"] = stepParams;
                steps[si] = step;
                break;
            }
        }

        // Unique pipeline id: pipe-<ci+1>-<seed_part>
        QString uid = QString("pipe-lhs-%1-%2")
            .arg(ci + 1, 4, 10, QChar('0'))
            .arg(static_cast<qulonglong>(seed));

        pipe["steps"] = steps;
        root["pipeline"]    = pipe;
        root["pipelineUID"] = uid;
        root["status"]      = "active";
        root["description"] = QString("lhs-generated config %1/%2 seed=%3")
                                  .arg(ci+1).arg(n).arg(static_cast<qulonglong>(seed));

        GeneratedConfig gc;
        gc.pipeline_id  = uid.toStdString();
        gc.config_json  = QString(QJsonDocument(root).toJson(QJsonDocument::Compact))
                              .toStdString();
        result.push_back(std::move(gc));
    }

    return result;
}
