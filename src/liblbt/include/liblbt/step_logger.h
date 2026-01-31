#pragma once

#include <string>
#include <functional>
#include <memory>
#include <sstream>

namespace lbt {

/**
 * @brief Simple logger interface for ProcessStep implementations
 * 
 * StepLogger provides a lightweight, thread-safe logging mechanism that can be
 * injected into ProcessStep instances. It allows centralized log collection
 * without introducing heavy dependencies like Qt or spdlog.
 */
class StepLogger {
public:
    enum class Level {
        DEBUG,
        INFO,
        WARNING,
        ERROR
    };

    using LogCallback = std::function<void(Level level, const std::string& message)>;

    /**
     * @brief Construct a logger with a custom callback
     * @param callback Function called for each log message
     */
    explicit StepLogger(LogCallback callback)
        : m_callback(std::move(callback)) {}

    /**
     * @brief Log a debug message
     */
    void debug(const std::string& message) const {
        log(Level::DEBUG, message);
    }

    /**
     * @brief Log an info message
     */
    void info(const std::string& message) const {
        log(Level::INFO, message);
    }

    /**
     * @brief Log a warning message
     */
    void warning(const std::string& message) const {
        log(Level::WARNING, message);
    }

    /**
     * @brief Log an error message
     */
    void error(const std::string& message) const {
        log(Level::ERROR, message);
    }

    /**
     * @brief Log a message with specified level
     */
    void log(Level level, const std::string& message) const {
        if (m_callback) {
            m_callback(level, message);
        }
    }

    /**
     * @brief Create a no-op logger (discards all messages)
     */
    static std::shared_ptr<StepLogger> createNull() {
        return std::make_shared<StepLogger>([](Level, const std::string&) {});
    }

    /**
     * @brief Create a console logger (stdout/stderr)
     */
    static std::shared_ptr<StepLogger> createConsole() {
        return std::make_shared<StepLogger>([](Level level, const std::string& message) {
            const char* prefix;
            switch (level) {
                case Level::DEBUG:   prefix = "[DEBUG] "; break;
                case Level::INFO:    prefix = "[INFO] "; break;
                case Level::WARNING: prefix = "[WARN] "; break;
                case Level::ERROR:   prefix = "[ERROR] "; break;
                default:             prefix = "[LOG] "; break;
            }
            
            if (level == Level::ERROR) {
                std::cerr << prefix << message << std::endl;
            } else {
                std::cout << prefix << message << std::endl;
            }
        });
    }

private:
    LogCallback m_callback;
};

} // namespace lbt
