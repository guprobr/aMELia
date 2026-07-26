#include "rag/sourceclassifier.h"

#include <QFileInfo>

QStringList extensions()
{
    return {
        QStringLiteral("*.txt"), QStringLiteral("*.md"), QStringLiteral("*.log"), QStringLiteral("*.yaml"),
        QStringLiteral("*.yml"), QStringLiteral("*.json"), QStringLiteral("*.ini"), QStringLiteral("*.cfg"),
        QStringLiteral("*.conf"), QStringLiteral("*.xml"), QStringLiteral("*.csv"), QStringLiteral("*.service"),
        QStringLiteral("*.sh"), QStringLiteral("*.py"), QStringLiteral("*.c"), QStringLiteral("*.cc"),
        QStringLiteral("*.cpp"), QStringLiteral("*.cxx"), QStringLiteral("*.h"), QStringLiteral("*.hpp"),
        QStringLiteral("*.cmake"), QStringLiteral("*.pdf"), QStringLiteral("*.docx")
    };
}

QString detectSourceType(const QFileInfo &info)
{
    const QString suffix = info.suffix().toLower();
    if (suffix == QStringLiteral("log")) {
        return QStringLiteral("log");
    }
    if (suffix == QStringLiteral("yaml") || suffix == QStringLiteral("yml") || suffix == QStringLiteral("json")
        || suffix == QStringLiteral("xml") || suffix == QStringLiteral("ini") || suffix == QStringLiteral("cfg")
        || suffix == QStringLiteral("conf") || suffix == QStringLiteral("service")) {
        return QStringLiteral("config");
    }
    if (suffix == QStringLiteral("md") || suffix == QStringLiteral("txt") || suffix == QStringLiteral("pdf")
        || suffix == QStringLiteral("docx") || suffix == QStringLiteral("csv")) {
        return QStringLiteral("doc");
    }
    if (suffix == QStringLiteral("cpp") || suffix == QStringLiteral("cxx") || suffix == QStringLiteral("cc")
        || suffix == QStringLiteral("c") || suffix == QStringLiteral("h") || suffix == QStringLiteral("hpp")
        || suffix == QStringLiteral("py") || suffix == QStringLiteral("sh") || suffix == QStringLiteral("cmake")) {
        return QStringLiteral("code");
    }
    return QStringLiteral("misc");
}

QString detectSourceRole(const QFileInfo &info, const QString &sourceType, const QString &text)
{
    const QString fingerprint = (info.fileName() + QLatin1Char(' ') + info.filePath() + QLatin1Char(' ') + text.left(4000)).toLower();

    if (sourceType == QStringLiteral("log") || fingerprint.contains(QStringLiteral("alarm")) || fingerprint.contains(QStringLiteral("traceback"))) {
        return QStringLiteral("log");
    }
    if (sourceType == QStringLiteral("config")) {
        return QStringLiteral("config");
    }
    if (sourceType == QStringLiteral("code")) {
        return QStringLiteral("code");
    }

    if (fingerprint.contains(QStringLiteral("hld")) || fingerprint.contains(QStringLiteral("lld"))
            || fingerprint.contains(QStringLiteral("topology")) || fingerprint.contains(QStringLiteral("architecture"))
            || fingerprint.contains(QStringLiteral("design")) || fingerprint.contains(QStringLiteral("scenario"))
            || fingerprint.contains(QStringLiteral("ciq"))) {
        return QStringLiteral("scenario");
    }

    if (fingerprint.contains(QStringLiteral("runbook")) || fingerprint.contains(QStringLiteral("mop"))
            || fingerprint.contains(QStringLiteral("operator training")) || fingerprint.contains(QStringLiteral("training manual"))
            || fingerprint.contains(QStringLiteral("procedure")) || fingerprint.contains(QStringLiteral("deployment"))
            || fingerprint.contains(QStringLiteral("install")) || fingerprint.contains(QStringLiteral("bootstrap"))
            || fingerprint.contains(QStringLiteral("workflow"))) {
        return QStringLiteral("procedure");
    }

    if (fingerprint.contains(QStringLiteral("guide")) || fingerprint.contains(QStringLiteral("manual"))
            || fingerprint.contains(QStringLiteral("reference")) || fingerprint.contains(QStringLiteral("concatenated"))
            || fingerprint.contains(QStringLiteral("release notes")) || fingerprint.contains(QStringLiteral("overview"))) {
        return QStringLiteral("reference");
    }

    return sourceType == QStringLiteral("doc") ? QStringLiteral("reference") : sourceType;
}
