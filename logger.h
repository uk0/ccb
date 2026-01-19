#ifndef LOGGER_H
#define LOGGER_H

#include <QString>
#include <QFile>
#include <QTextStream>
#include <QMutex>
#include <QDateTime>

class Logger
{
public:
    static Logger& instance();

    void log(const QString &message);
    void setLogFile(const QString &path);
    void setEnabled(bool enabled);

private:
    Logger();
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    QFile m_file;
    QTextStream m_stream;
    QMutex m_mutex;
    bool m_enabled = true;
};

// Convenience macro
#define LOG(msg) Logger::instance().log(msg)

#endif // LOGGER_H
