#include "logger.h"
#include <QMutexLocker>
#include <QDir>
#include <QStandardPaths>

Logger& Logger::instance()
{
    static Logger instance;
    return instance;
}

Logger::Logger()
{
#ifdef Q_OS_WIN
    setLogFile(QDir::toNativeSeparators("C:/ccb.log"));
#elif defined(Q_OS_LINUX)
    setLogFile("/var/logs/ccb.log");
#else
    setLogFile("/tmp/ccb.log");
#endif
}

Logger::~Logger()
{
    if (m_file.isOpen()) {
        m_stream.flush();
        m_file.close();
    }
}

void Logger::setLogFile(const QString &path)
{
    QMutexLocker locker(&m_mutex);

    if (m_file.isOpen()) {
        m_stream.flush();
        m_file.close();
    }

    m_file.setFileName(path);
    if (m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        m_stream.setDevice(&m_file);
        m_stream << "\n\n========== CCB Started at "
                 << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss")
                 << " ==========\n";
        m_stream.flush();
    }
}

void Logger::setEnabled(bool enabled)
{
    m_enabled = enabled;
}

void Logger::log(const QString &message)
{
    if (!m_enabled) return;

    QMutexLocker locker(&m_mutex);

    if (m_file.isOpen()) {
        QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
        m_stream << "[" << timestamp << "] " << message << "\n";
        m_stream.flush();
    }
}
