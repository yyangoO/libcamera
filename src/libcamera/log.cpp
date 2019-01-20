/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2018, Google Inc.
 *
 * log.cpp - Logging infrastructure
 */

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <list>
#include <string.h>
#include <unordered_set>

#include "log.h"
#include "utils.h"

/**
 * \file log.h
 * \brief Logging infrastructure
 *
 * libcamera includes a logging infrastructure used through the library that
 * allows inspection of internal operation in a user-configurable way. The log
 * messages are grouped in categories that represent areas of libcamera, and
 * output of messages for each category can be controlled by independent log
 * levels.
 *
 * The levels are configurable through the LIBCAMERA_LOG_LEVELS environment
 * variable that contains a comma-separated list of 'category=level' pairs.
 *
 * The category names are strings and can include a wildcard ('*') character at
 * the end to match multiple categories.
 *
 * The level are either numeric values, or strings containing the log level
 * name. The available log levels are DEBUG, INFO, WARN, ERROR and FATAL. Log
 * message with a level higher than or equal to the configured log level for
 * their category are output to the log, while other messages are silently
 * discarded.
 */

namespace libcamera {

/**
 * \brief Message logger
 *
 * The Logger class handles log configuration.
 */
class Logger
{
public:
	static Logger *instance();

private:
	Logger();

	void parseLogLevels();
	static LogSeverity parseLogLevel(const std::string &level);

	friend LogCategory;
	void registerCategory(LogCategory *category);
	void unregisterCategory(LogCategory *category);

	std::unordered_set<LogCategory *> categories_;
	std::list<std::pair<std::string, LogSeverity>> levels_;
};

/**
 * \brief Retrieve the logger instance
 *
 * The Logger is a singleton and can't be constructed manually. This function
 * shall instead be used to retrieve the single global instance of the logger.
 *
 * \return The logger instance
 */
Logger *Logger::instance()
{
	static Logger instance;
	return &instance;
}

/**
 * \brief Construct a logger
 */
Logger::Logger()
{
	parseLogLevels();
}

/**
 * \brief Parse the log levels from the environment
 *
 * The logr levels are stored in LIBCAMERA_LOG_LEVELS environement variable as a list
 * of "category=level" pairs, separated by commas (','). Parse the variable and
 * store the levels to configure all log categories.
 */
void Logger::parseLogLevels()
{
	const char *debug = secure_getenv("LIBCAMERA_LOG_LEVELS");
	if (!debug)
		return;

	for (const char *pair = debug; *debug != '\0'; pair = debug) {
		const char *comma = strchrnul(debug, ',');
		size_t len = comma - pair;

		/* Skip over the comma. */
		debug = *comma == ',' ? comma + 1 : comma;

		/* Skip to the next pair if the pair is empty. */
		if (!len)
			continue;

		std::string category;
		std::string level;

		const char *colon = static_cast<const char *>(memchr(pair, ':', len));
		if (!colon) {
			/* 'x' is a shortcut for '*:x'. */
			category = "*";
			level = std::string(pair, len);
		} else {
			category = std::string(pair, colon - pair);
			level = std::string(colon + 1, comma - colon - 1);
		}

		/* Both the category and the level must be specified. */
		if (category.empty() || level.empty())
			continue;

		LogSeverity severity = parseLogLevel(level);
		if (severity == -1)
			continue;

		levels_.push_back({ category, severity });
	}
}

/**
 * \brief Parse a log level string into a LogSeverity
 * \param[in] level The log level string
 *
 * Log levels can be specified as an integer value in the range from LogDebug to
 * LogFatal, or as a string corresponding to the severity name in uppercase. Any
 * other value is invalid.
 *
 * \return The log severity, or -1 if the string is invalid
 */
LogSeverity Logger::parseLogLevel(const std::string &level)
{
	static const char *const names[] = {
		"DEBUG",
		"INFO",
		"WARN",
		"ERROR",
		"FATAL",
	};

	int severity;

	if (std::isdigit(level[0])) {
		char *endptr;
		severity = strtoul(level.c_str(), &endptr, 10);
		if (*endptr != '\0' || severity > LogFatal)
			severity = -1;
	} else {
		severity = -1;
		for (unsigned int i = 0; i < ARRAY_SIZE(names); ++i) {
			if (names[i] == level) {
				severity = i;
				break;
			}
		}
	}

	return static_cast<LogSeverity>(severity);
}

/**
 * \brief Register a log category with the logger
 * \param[in] category The log category
 *
 * Log categories must have unique names. If a category with the same name
 * already exists this function performs no operation.
 */
void Logger::registerCategory(LogCategory *category)
{
	categories_.insert(category);

	const std::string &name = category->name();
	for (const std::pair<std::string, LogSeverity> &level : levels_) {
		bool match = true;

		for (unsigned int i = 0; i < level.first.size(); ++i) {
			if (level.first[i] == '*')
				break;

			if (i >= name.size() ||
			    name[i] != level.first[i]) {
				match = false;
				break;
			}
		}

		if (match) {
			category->setSeverity(level.second);
			break;
		}
	}
}

/**
 * \brief Unregister a log category from the logger
 * \param[in] category The log category
 *
 * If the \a category hasn't been registered with the logger this function
 * performs no operation.
 */
void Logger::unregisterCategory(LogCategory *category)
{
	categories_.erase(category);
}

/**
 * \enum LogSeverity
 * Log message severity
 * \var LogDebug
 * Debug message
 * \var LogInfo
 * Informational message
 * \var LogWarning
 * Warning message, signals a potential issue
 * \var LogError
 * Error message, signals an unrecoverable issue
 * \var LogFatal
 * Fatal message, signals an unrecoverable issue and aborts execution
 */

/**
 * \class LogCategory
 * \brief A category of log message
 *
 * The LogCategory class represents a category of log messages, related to an
 * area of the library. It groups all messages belonging to the same category,
 * and is used to control the log level per group.
 */

/**
 * \brief Construct a log category
 * \param[in] name The category name
 */
LogCategory::LogCategory(const char *name)
	: name_(name), severity_(LogSeverity::LogInfo)
{
	Logger::instance()->registerCategory(this);
}

LogCategory::~LogCategory()
{
	Logger::instance()->unregisterCategory(this);
}

/**
 * \fn LogCategory::name()
 * \brief Retrieve the log category name
 * \return The log category name
 */

/**
 * \fn LogCategory::severity()
 * \brief Retrieve the severity of the log category
 * \sa setSeverity()
 * \return Return the severity of the log category
 */

/**
 * \brief Set the severity of the log category
 *
 * Messages of severity higher than or equal to the severity of the log category
 * are printed, other messages are discarded.
 */
void LogCategory::setSeverity(LogSeverity severity)
{
	severity_ = severity;
}

/**
 * \brief Retrieve the default log category
 *
 * The default log category is named "default" and is used by the LOG() macro
 * when no log category is specified.
 *
 * \return A pointer to the default log category
 */
const LogCategory &LogCategory::defaultCategory()
{
	static LogCategory category("default");
	return category;
}

static const char *log_severity_name(LogSeverity severity)
{
	static const char *const names[] = {
		"  DBG",
		" INFO",
		" WARN",
		"  ERR",
		"FATAL",
	};

	if (static_cast<unsigned int>(severity) < ARRAY_SIZE(names))
		return names[severity];
	else
		return "UNKWN";
}

/**
 * \class LogMessage
 * \brief Internal log message representation.
 *
 * The LogMessage class models a single message in the log. It serves as a
 * helper to provide the std::ostream API for logging, and must never be used
 * directly. Use the LOG() macro instead access the log infrastructure.
 */

/**
 * \brief Construct a log message for the default category
 * \param[in] fileName The file name where the message is logged from
 * \param[in] line The line number where the message is logged from
 * \param[in] severity The log message severity, controlling how the message
 * will be displayed
 *
 * Create a log message pertaining to line \a line of file \a fileName. The
 * \a severity argument sets the message severity to control whether it will be
 * output or dropped.
 */
LogMessage::LogMessage(const char *fileName, unsigned int line,
		       LogSeverity severity)
	: category_(LogCategory::defaultCategory()), severity_(severity)
{
	init(fileName, line);
}

/**
 * \brief Construct a log message for a given category
 * \param[in] fileName The file name where the message is logged from
 * \param[in] line The line number where the message is logged from
 * \param[in] category The log message category, controlling how the message
 * will be displayed
 * \param[in] severity The log message severity, controlling how the message
 * will be displayed
 *
 * Create a log message pertaining to line \a line of file \a fileName. The
 * \a severity argument sets the message severity to control whether it will be
 * output or dropped.
 */
LogMessage::LogMessage(const char *fileName, unsigned int line,
		       const LogCategory &category, LogSeverity severity)
	: category_(category), severity_(severity)
{
	init(fileName, line);
}

void LogMessage::init(const char *fileName, unsigned int line)
{
	/* Log the timestamp, severity and file information. */
	struct timespec timestamp;
	clock_gettime(CLOCK_MONOTONIC, &timestamp);
	msgStream_.fill('0');
	msgStream_ << "[" << timestamp.tv_sec / (60 * 60) << ":"
		   << std::setw(2) << (timestamp.tv_sec / 60) % 60 << ":"
		   << std::setw(2) << timestamp.tv_sec % 60 << "."
		   << std::setw(9) << timestamp.tv_nsec << "]";
	msgStream_.fill(' ');

	msgStream_ << " " << log_severity_name(severity_);
	msgStream_ << " " << category_.name();
	msgStream_ << " " << basename(fileName) << ":" << line << " ";
}

LogMessage::~LogMessage()
{
	msgStream_ << std::endl;

	if (severity_ >= category_.severity()) {
		std::string msg(msgStream_.str());
		fwrite(msg.data(), msg.size(), 1, stderr);
		fflush(stderr);
	}

	if (severity_ == LogSeverity::LogFatal)
		std::abort();
}

/**
 * \fn std::ostream& LogMessage::stream()
 *
 * Data is added to a LogMessage through the stream returned by this function.
 * The stream implements the std::ostream API and can be used for logging as
 * std::cout.
 *
 * \return A reference to the log message stream
 */

/**
 * \def LOG_DECLARE_CATEGORY(name)
 * \hideinitializer
 * \brief Declare a category of log messages
 *
 * This macro is used to declare a log category defined in another compilation
 * unit by the LOG_DEFINE_CATEGORY() macro.
 *
 * The LOG_DECLARE_CATEGORY() macro must be used in the libcamera namespace.
 *
 * \sa LogCategory
 */

/**
 * \def LOG_DEFINE_CATEGORY(name)
 * \hideinitializer
 * \brief Define a category of log messages
 *
 * This macro is used to define a log category that can then be used with the
 * LOGC() macro. Category names shall be unique, if a category is shared between
 * compilation units, it shall be defined in one compilation unit only and
 * declared with LOG_DECLARE_CATEGORY() in the other compilation units.
 *
 * The LOG_DEFINE_CATEGORY() macro must be used in the libcamera namespace.
 *
 * \sa LogCategory
 */

/**
 * \def LOG(category, severity)
 * \hideinitializer
 * \brief Log a message
 * \param[in] category Category (optional)
 * \param[in] severity Severity
 *
 * Return an std::ostream reference to which a message can be logged using the
 * iostream API. The \a category, if specified, sets the message category. When
 * absent the default category is used. The  \a severity controls whether the
 * message is printed or discarded, depending on the log level for the category.
 *
 * If the severity is set to Fatal, execution is aborted and the program
 * terminates immediately after printing the message.
 */

/**
 * \def ASSERT(condition)
 * \brief Abort program execution if assertion fails
 *
 * If \a condition is false, ASSERT() logs an error message with the Fatal log
 * level and aborts program execution.
 *
 * If the macro NDEBUG is defined before including log.h, ASSERT() generates no
 * code.
 *
 * Using conditions that have side effects with ASSERT() is not recommended, as
 * these effects would depend on whether NDEBUG is defined or not. Similarly,
 * ASSERT() should not be used to check for errors that can occur under normal
 * conditions as those checks would then be removed when compiling with NDEBUG.
 */

} /* namespace libcamera */
