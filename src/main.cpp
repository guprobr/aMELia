#include "appconfig.h"
#include "chatcontroller.h"
#include "mainwindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QStringList>
#include <QTextStream>

namespace {
QString preferredUserConfigPath()
{
    return QDir::home().filePath(QStringLiteral(".amelia_qt6/config.json"));
}

QString builtInDefaultConfig()
{
    return QStringLiteral(
R"JSON({
  "ollamaBaseUrl": "http://127.0.0.1:11434",
  "ollamaModel": "qwen2.5-coder:14b",
  "docsRoot": "./docs/sample",
  "dataRoot": "${HOME}/.amelia_qt6",
  "knowledgeRoot": "${HOME}/.amelia_qt6/knowledge",
  "enableExternalSearch": false,
  "autoSuggestExternalSearch": false,
  "probeOllamaOnStartup": true,
  "restoreLastConversationOnStartup": true,
  "autoPersistMemories": true,
  "autoSaveSessionSummary": true,
  "seedDocsIntoKnowledge": true,
  "enableSemanticRetrieval": true,
  "preferOutlinePlanning": true,
  "requireGroundingForProjectQuestions": true,
  "includeAssistantHistoryInPrompt": false,
  "searxngUrl": "http://127.0.0.1:8080/search",
  "maxHistoryTurns": 4,
  "maxLocalHits": 3,
  "maxExternalHits": 2,
  "maxRelevantMemories": 6,
  "externalSearchTimeoutMs": 15000,
  "ollamaProbeTimeoutMs": 10000,
  "ollamaResponseHeadersTimeoutMs": 180000,
  "ollamaFirstTokenTimeoutMs": 600000,
  "ollamaInactivityTimeoutMs": 300000,
  "ollamaTotalTimeoutMs": 0,
  "maxDiagnosticLines": 400,
  "ollamaNumCtx": 32768,
  "ollamaTemperature": 0.15,
  "ollamaTopP": 0.95,
  "ollamaTopK": 50,
  "ollamaRepeatPenalty": 1.12,
  "ollamaPresencePenalty": 0.0,
  "ollamaFrequencyPenalty": 0.0,
  "ollamaStopSequences": ["<END>"],
  "externalSearchDomainAllowlist": []
})JSON");
}

bool ensureFileWritten(const QString &path, const QByteArray &data, QString *message)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (message != nullptr) {
            *message = QStringLiteral("Failed to write %1").arg(QDir::toNativeSeparators(path));
        }
        return false;
    }
    if (file.write(data) < 0) {
        if (message != nullptr) {
            *message = QStringLiteral("Failed to write %1").arg(QDir::toNativeSeparators(path));
        }
        return false;
    }
    file.close();
    return true;
}

void ensureUserDataRoot(QStringList &messages)
{
    const QString dataRoot = ameliaDefaultDataRoot();
    const QString knowledgeRoot = QDir(dataRoot).filePath(QStringLiteral("knowledge"));

    QDir dir;
    for (const QString &path : {dataRoot, knowledgeRoot}) {
        if (!dir.exists(path) && dir.mkpath(path)) {
            messages << QStringLiteral("Created %1").arg(QDir::toNativeSeparators(path));
        }
    }
}

void ensureUserConfigSeeded(QStringList &messages)
{
    const QString configPath = preferredUserConfigPath();
    const QFileInfo info(configPath);
    QDir dir;
    if (!dir.exists(info.path())) {
        if (dir.mkpath(info.path())) {
            messages << QStringLiteral("Created %1").arg(QDir::toNativeSeparators(info.path()));
        }
    }

    if (QFileInfo::exists(configPath)) {
        return;
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath(QStringLiteral("../share/amelia_qt6/config/config.example.json")),
        QDir(appDir).filePath(QStringLiteral("../../share/amelia_qt6/config/config.example.json")),
        QDir(appDir).filePath(QStringLiteral("config/config.example.json")),
        QDir::current().filePath(QStringLiteral("config/config.example.json"))
    };

    for (const QString &candidate : candidates) {
        QFile source(candidate);
        if (!source.exists()) {
            continue;
        }
        if (!source.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        const QByteArray data = source.readAll();
        source.close();
        QString writeMessage;
        if (ensureFileWritten(configPath, data, &writeMessage)) {
            messages << QStringLiteral("Seeded user config from %1 to %2")
                            .arg(QDir::toNativeSeparators(candidate),
                                 QDir::toNativeSeparators(configPath));
            return;
        }
    }

    QString writeMessage;
    if (ensureFileWritten(configPath, builtInDefaultConfig().toUtf8(), &writeMessage)) {
        messages << QStringLiteral("Seeded user config at %1 from built-in defaults")
                        .arg(QDir::toNativeSeparators(configPath));
    }
}
}

static QString resolveConfigPath(QString *message)
{
    const QString current = QDir::currentPath();
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString homeConfig = preferredUserConfigPath();

    const QStringList candidates = {
        homeConfig,
        QDir(current).filePath(QStringLiteral("config/config.json")),
        QDir(appDir).filePath(QStringLiteral("config/config.json")),
        QDir(appDir).filePath(QStringLiteral("../config/config.json")),
        QDir(appDir).filePath(QStringLiteral("../../config/config.json"))
    };

    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            if (message != nullptr) {
                *message = QStringLiteral("Loaded config from %1").arg(QDir::toNativeSeparators(candidate));
            }
            return candidate;
        }
    }

    if (message != nullptr) {
        *message = QStringLiteral("No config found. Preferred path is ~/.amelia_qt6/config.json. Using defaults.");
    }
    return homeConfig;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/branding/amelia_logo.svg")));
    QApplication::setApplicationName(QStringLiteral("amelia_qt6"));
    QApplication::setOrganizationName(QStringLiteral("guprobr"));

    QStringList bootstrapMessages;
    ensureUserDataRoot(bootstrapMessages);
    ensureUserConfigSeeded(bootstrapMessages);

    QString configPathMessage;
    const QString configPath = resolveConfigPath(&configPathMessage);

    QString configLoadMessage;
    const AppConfig config = AppConfigLoader::load(configPath, &configLoadMessage);

    MainWindow window;
    ChatController controller(config);

    QObject::connect(&window, &MainWindow::promptSubmitted,
                     &controller, &ChatController::sendUserPrompt);
    QObject::connect(&window, &MainWindow::stopRequested,
                     &controller, &ChatController::stopGeneration);
    QObject::connect(&window, &MainWindow::reindexRequested,
                     &controller, &ChatController::reindexDocs);
    QObject::connect(&window, &MainWindow::testBackendRequested,
                     &controller, &ChatController::probeBackend);
    QObject::connect(&window, &MainWindow::refreshModelsRequested,
                     &controller, &ChatController::refreshBackendModels);
    QObject::connect(&window, &MainWindow::newConversationRequested,
                     &controller, &ChatController::newConversation);
    QObject::connect(&window, &MainWindow::conversationSelected,
                     &controller, &ChatController::loadConversationById);
    QObject::connect(&window, &MainWindow::rememberRequested,
                     &controller, &ChatController::rememberNote);
    QObject::connect(&window, &MainWindow::backendModelSelected,
                     &controller, &ChatController::setBackendModel);
    QObject::connect(&window, &MainWindow::importPathsRequested,
                     &controller, &ChatController::importKnowledgePaths);
    QObject::connect(&window, &MainWindow::clearMemoriesRequested,
                     &controller, &ChatController::clearMemories);

    QObject::connect(&controller, &ChatController::assistantStreamChunk,
                     &window, &MainWindow::appendAssistantChunk);
    QObject::connect(&controller, &ChatController::assistantCompleted,
                     &window, &MainWindow::finalizeAssistantMessage);
    QObject::connect(&controller, &ChatController::systemNotice,
                     &window, &MainWindow::appendSystemMessage);
    QObject::connect(&controller, &ChatController::privacyPreviewReady,
                     &window, &MainWindow::setPrivacyPreview);
    QObject::connect(&controller, &ChatController::localSourcesReady,
                     &window, &MainWindow::setLocalSources);
    QObject::connect(&controller, &ChatController::externalSourcesReady,
                     &window, &MainWindow::setExternalSources);
    QObject::connect(&controller, &ChatController::outlinePlanReady,
                     &window, &MainWindow::setOutlinePlan);
    QObject::connect(&controller, &ChatController::memoriesViewReady,
                     &window, &MainWindow::setMemoriesView);
    QObject::connect(&controller, &ChatController::sessionSummaryReady,
                     &window, &MainWindow::setSessionSummary);
    QObject::connect(&controller, &ChatController::transcriptRestored,
                     &window, &MainWindow::setTranscript);
    QObject::connect(&controller, &ChatController::conversationListReady,
                     &window, &MainWindow::setConversationList);
    QObject::connect(&controller, &ChatController::busyChanged,
                     &window, &MainWindow::setBusy);
    QObject::connect(&controller, &ChatController::statusChanged,
                     &window, &MainWindow::setStatusText);
    QObject::connect(&controller, &ChatController::backendSummaryReady,
                     &window, &MainWindow::setBackendSummary);
    QObject::connect(&controller, &ChatController::diagnosticsReady,
                     &window, &MainWindow::setDiagnostics);
    QObject::connect(&controller, &ChatController::sourceInventoryReady,
                     &window, &MainWindow::setSourceInventory);
    QObject::connect(&controller, &ChatController::backendModelsReady,
                     &window, &MainWindow::setAvailableModels);

    window.setExternalSearchEnabledDefault(config.enableExternalSearch);

    for (const QString &bootstrapMessage : bootstrapMessages) {
        if (!bootstrapMessage.isEmpty()) {
            window.appendSystemMessage(bootstrapMessage);
        }
    }
    if (!configPathMessage.isEmpty()) {
        window.appendSystemMessage(configPathMessage);
    }
    if (!configLoadMessage.isEmpty()) {
        window.appendSystemMessage(configLoadMessage);
    }

    window.show();
    return app.exec();
}
