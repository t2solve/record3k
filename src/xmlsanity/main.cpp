#include <iostream>
#include <liblbt/pipeline_config_loader.h>

int main(int argc, char** argv) {
    std::string path;
    if (argc > 1) path = argv[1];
    else {
        std::cerr << "Usage: xmlsanity <pipeline.xml>\n";
        return 2;
    }

    try {
        auto cfg = PipelineConfigLoader::loadFromXML(path);
        std::cout << "Mode: " << PipelineConfigLoader::processingModeToString(cfg.mode) << "\n";
        std::cout << "Steps: " << cfg.steps.size() << ", Configs: " << cfg.configs.size() << "\n";
        for (size_t i = 0; i < cfg.steps.size(); ++i) {
            std::cout << i << ": " << cfg.steps[i]->getName();
            auto params = cfg.configs[i].getAllParameters();
            std::cout << " (" << params.size() << " params)\n";
            for (const auto& kv : params) {
                std::cout << "  - " << kv.first << " = " << kv.second << "\n";
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
