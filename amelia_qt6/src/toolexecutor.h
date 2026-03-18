#pragma once

#include <QString>
#include <QStringList>

struct ToolResult {
    bool ok = false;
    QString stdOut;
    QString stdErr;
    int exitCode = -1;
};

class ToolExecutor {
public:
    QStringList allowedCommands() const;
    ToolResult runCommand(const QString &program,
                          const QStringList &arguments,
                          int timeoutMs = 15000) const;
};
