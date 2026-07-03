#pragma once
#include <string>
#include <vector>
#include <cstdint>

// Generates pipeline JSON configs via Latin Hypercube Sampling over the
// "pipeline.*" entries in param_ranges.json, applied on top of a base pipeline.
class PipelineConfigGen {
public:
    struct GeneratedConfig {
        std::string pipeline_id;
        std::string config_json;  // full pipeline JSON string, ready for PipelineConfigLoader
    };

    // base_pipeline_json: the starting-point pipeline JSON string.
    // param_ranges_path:  path to param_ranges.json (only "pipeline.*" keys are used).
    // n:                  number of configs to generate.
    // seed:               random seed for reproducibility.
    static std::vector<GeneratedConfig> generate(
        const std::string& base_pipeline_json,
        const std::string& param_ranges_path,
        int                n,
        uint64_t           seed = 42);

    // Returns the hardcoded default synthetic pipeline JSON string.
    static std::string defaultBasePipelineJson();
};
