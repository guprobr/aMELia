#include "toolexecutor.h"

#include <QProcess>
#include <QSet>

QStringList ToolExecutor::allowedCommands() const
{
    return {
        QStringLiteral("grep"),
        QStringLiteral("cat"),
        QStringLiteral("awk"),
        QStringLiteral("sed"),
        QStringLiteral("kubectl"),
        QStringLiteral("helm"),
        QStringLiteral("journalctl"),
        QStringLiteral("system"),
        QStringLiteral("dcmanager")
    };
}

ToolResult ToolExecutor::runCommand(const QString &program,
                                    const QStringList &arguments,
                                    int timeoutMs) const
{
    ToolResult result;
    const QSet<QString> whitelist(allowedCommands().cbegin(), allowedCommands().cend());

    if (!whitelist.contains(program)) {
        result.stdErr = QStringLiteral("Command is not allowed: %1").arg(program);
        return result;
    }

    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    process.start();

    if (!process.waitForStarted(timeoutMs)) {
        result.stdErr = QStringLiteral("Failed to start command: %1").arg(program);
        return result;
    }

    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(2000);
        result.stdErr = QStringLiteral("Command timed out: %1").arg(program);
        return result;
    }

    result.exitCode = process.exitCode();
    result.stdOut = QString::fromUtf8(process.readAllStandardOutput());
    result.stdErr = QString::fromUtf8(process.readAllStandardError());
    result.ok = (process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0);
    return result;
}
