#ifndef __LOGGER_HPP__
#define __LOGGER_HPP__

#include <chrono>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

// ANSI color codes
#define RESET "\033[0m"
#define BLACK "\033[30m"
#define RED "\033[31m"
#define GREEN "\033[32m"
#define YELLOW "\033[33m"
#define BLUE "\033[34m"
#define MAGENTA "\033[35m"
#define CYAN "\033[36m"
#define WHITE "\033[37m"
#define BOLD_BLACK "\033[1m\033[30m"
#define BOLD_RED "\033[1m\033[31m"
#define BOLD_GREEN "\033[1m\033[32m"
#define BOLD_YELLOW "\033[1m\033[33m"
#define BOLD_BLUE "\033[1m\033[34m"
#define BOLD_MAGENTA "\033[1m\033[35m"
#define BOLD_CYAN "\033[1m\033[36m"
#define BOLD_WHITE "\033[1m\033[37m"

/**
 * @class Logger
 * @brief Simple logging system with different log levels
 *
 * This class provides a simple logging system with different log levels
 * (DEBUG, INFO, WARNING, CAKE_ERROR) and thread-safe logging.
 */

class Logger {
public:
    enum class Level { DEBUG, INFO, WARNING, CAKE_ERROR };

    /**
     * @brief Get the singleton instance of the logger
     * @return Reference to the logger instance
     */
    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }

    /**
     * @brief Set the log level
     * @param level The minimum log level to display
     */
    void setLevel(Level level) { level_ = level; }

    /**
     * @brief Get the current log level
     * @return The current log level
     */
    Level getLevel() const { return level_; }

    /**
     * @brief Set whether to show timestamps in log messages
     * @param show True to show timestamps, false otherwise
     */
    void showTimestamps(bool show) { showTimestamps_ = show; }

    /**
     * @brief Log a debug message
     * @param message The message to log
     */
    void debug(std::string const& message) { log(Level::DEBUG, message); }

    /**
     * @brief Log an info message
     * @param message The message to log
     */
    void info(std::string const& message) { log(Level::INFO, message); }

    /**
     * @brief Log a warning message
     * @param message The message to log
     */
    void warning(std::string const& message) { log(Level::WARNING, message); }

    /**
     * @brief Log an error message
     * @param message The message to log
     */
    void error(std::string const& message) { log(Level::CAKE_ERROR, message); }

private:
    Logger() : level_(Level::INFO), showTimestamps_(false) {}

    ~Logger() = default;
    Logger(Logger const&) = delete;
    Logger& operator=(Logger const&) = delete;

    /**
     * @brief Log a message with the specified level
     * @param level The log level
     * @param message The message to log
     */
    void log(Level level, std::string const& message) {
        if (level < level_) { return; }

        std::lock_guard<std::mutex> lock(mutex_);

        if (showTimestamps_) { std::cout << getTimestamp() << " "; }

        std::cout << getLevelString(level) << ": " << message << RESET << std::endl;
    }

    /**
     * @brief Get a string representation of the log level
     * @param level The log level
     * @return String representation of the log level
     */
    std::string getLevelString(Level level) const {
        switch (level) {
            case Level::DEBUG:
                return BLUE "DEBUG";
            case Level::INFO:
                return GREEN "INFO";
            case Level::WARNING:
                return YELLOW "WARNING";
            case Level::CAKE_ERROR:
                return RED "CAKE_ERROR";
            default:
                return WHITE "UNKNOWN";
        }
    }

    /**
     * @brief Get a timestamp string for the current time
     * @return Timestamp string in the format [YYYY-MM-DD HH:MM:SS]
     */
    std::string getTimestamp() const {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << "[" << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S") << "]";
        return ss.str();
    }

    Level level_;
    bool showTimestamps_;
    std::mutex mutex_;
};

// Convenience macros for logging
#define LOG_DEBUG(message) Logger::getInstance().debug(message)
#define LOG_INFO(message) Logger::getInstance().info(message)
#define LOG_WARNING(message) Logger::getInstance().warning(message)
#define LOG_ERROR(message) Logger::getInstance().error(message)

#endif   // __LOGGER_HPP__
