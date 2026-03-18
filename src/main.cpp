#include "appconfig.h"
#include "appversion.h"
#include "bootstrapdialog.h"
#include "chatcontroller.h"
#include "mainwindow.h"
#include "notificationcenter.h"

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
  "enableExternalSearch": true,
  "autoSuggestExternalSearch": true,
  "probeOllamaOnStartup": true,
  "restoreLastConversationOnStartup": true,
  "autoPersistMemories": true,
  "autoSaveSessionSummary": true,
  "seedDocsIntoKnowledge": true,
  "enableSemanticRetrieval": true,
  "enableDesktopNotifications": true,
  "notifyOnTaskStart": true,
  "notifyOnTaskSuccess": true,
  "notifyOnTaskFailure": true,
  "preferOutlinePlanning": true,
  "requireGroundingForProjectQuestions": true,
  "includeAssistantHistoryInPrompt": true,
  "searxngUrl": "http://127.0.0.1:8080/search",
  "maxHistoryTurns": 8,
  "maxLocalHits": 8,
  "maxExternalHits": 2,
  "maxRelevantMemories": 6,
  "externalSearchTimeoutMs": 15000,
  "desktopNotificationTimeoutMs": 2500,
  "ollamaProbeTimeoutMs": 10000,
  "ollamaResponseHeadersTimeoutMs": 1800000,
  "ollamaFirstTokenTimeoutMs": 600000,
  "ollamaInactivityTimeoutMs": 300000,
  "ollamaTotalTimeoutMs": 0,
  "maxDiagnosticLines": 400,
  "ollamaNumCtx": 32768,
  "ollamaTemperature": 0.05,
  "ollamaTopP": 0.90,
  "ollamaTopK": 40,
  "ollamaRepeatPenalty": 1.10,
  "ollamaPresencePenalty": 0.0,
  "ollamaFrequencyPenalty": 0.0,
  "ollamaStopSequences": ["<END>", "<|im_end|>", "<|endoftext|>"],
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
    QApplication::setApplicationDisplayName(QStringLiteral("Amelia Qt6 v%1").arg(QLatin1StringView(AmeliaVersion::kDisplayVersion)));
    QApplication::setOrganizationName(QStringLiteral("guprobr"));
    QApplication::setQuitOnLastWindowClosed(true);

    BootstrapDialog bootstrap;
    bootstrap.show();
    bootstrap.setStatusText(QStringLiteral("Preparing Amelia runtime..."));
    app.processEvents();

    QStringList bootstrapMessages;
    ensureUserDataRoot(bootstrapMessages);
    ensureUserConfigSeeded(bootstrapMessages);
    for (const QString &message : bootstrapMessages) {
        bootstrap.appendLog(message);
    }
    app.processEvents();

    QString configPathMessage;
    const QString configPath = resolveConfigPath(&configPathMessage);
    if (!configPathMessage.isEmpty()) {
        bootstrap.appendLog(configPathMessage);
    }

    QString configLoadMessage;
    const AppConfig config = AppConfigLoader::load(configPath, &configLoadMessage);
    if (!configLoadMessage.isEmpty()) {
        bootstrap.appendLog(configLoadMessage);
    }
    bootstrap.setStatusText(QStringLiteral("Building Amelia interface..."));
    app.processEvents();

    auto *window = new MainWindow();
    auto *notifications = new NotificationCenter(config, &app);
    notifications->setAlertWidget(window);

    bootstrap.appendLog(QStringLiteral("Main window created."));
    bootstrap.setStatusText(QStringLiteral("Loading controller and cached knowledge..."));
    app.processEvents();

    auto *controller = new ChatController(config);
    bootstrap.appendLog(QStringLiteral("Controller initialized."));

    QObject::connect(controller, &ChatController::statusChanged,
                     &bootstrap, &BootstrapDialog::setStatusText);
    QObject::connect(controller, &ChatController::systemNotice,
                     &bootstrap, &BootstrapDialog::appendLog);

    QObject::connect(window, &MainWindow::promptSubmitted,
                     controller, &ChatController::sendUserPrompt);
    QObject::connect(window, &MainWindow::stopRequested,
                     controller, &ChatController::stopGeneration);
    QObject::connect(window, &MainWindow::reindexRequested,
                     controller, &ChatController::reindexDocs);
    QObject::connect(window, &MainWindow::testBackendRequested,
                     controller, &ChatController::probeBackend);
    QObject::connect(window, &MainWindow::refreshModelsRequested,
                     controller, &ChatController::refreshBackendModels);
    QObject::connect(window, &MainWindow::newConversationRequested,
                     controller, &ChatController::newConversation);
    QObject::connect(window, &MainWindow::conversationSelected,
                     controller, &ChatController::loadConversationById);
    QObject::connect(window, &MainWindow::rememberRequested,
                     controller, &ChatController::rememberNote);
    QObject::connect(window, &MainWindow::backendModelSelected,
                     controller, &ChatController::setBackendModel);
    QObject::connect(window, &MainWindow::importPathsRequested,
                     controller, &ChatController::importKnowledgePaths);
    QObject::connect(window, &MainWindow::clearMemoriesRequested,
                     controller, &ChatController::clearMemories);

    QObject::connect(controller, &ChatController::assistantStreamChunk,
                     window, &MainWindow::appendAssistantChunk);
    QObject::connect(controller, &ChatController::assistantCompleted,
                     window, &MainWindow::finalizeAssistantMessage);
    QObject::connect(controller, &ChatController::systemNotice,
                     window, &MainWindow::appendSystemMessage);
    QObject::connect(controller, &ChatController::privacyPreviewReady,
                     window, &MainWindow::setPrivacyPreview);
    QObject::connect(controller, &ChatController::localSourcesReady,
                     window, &MainWindow::setLocalSources);
    QObject::connect(controller, &ChatController::externalSourcesReady,
                     window, &MainWindow::setExternalSources);
    QObject::connect(controller, &ChatController::outlinePlanReady,
                     window, &MainWindow::setOutlinePlan);
    QObject::connect(controller, &ChatController::memoriesViewReady,
                     window, &MainWindow::setMemoriesView);
    QObject::connect(controller, &ChatController::sessionSummaryReady,
                     window, &MainWindow::setSessionSummary);
    QObject::connect(controller, &ChatController::transcriptRestored,
                     window, &MainWindow::setTranscript);
    QObject::connect(controller, &ChatController::conversationListReady,
                     window, &MainWindow::setConversationList);
    QObject::connect(controller, &ChatController::busyChanged,
                     window, &MainWindow::setBusy);
    QObject::connect(controller, &ChatController::indexingStateChanged,
                     window, &MainWindow::setIndexingActive);
    QObject::connect(controller, &ChatController::indexingProgressChanged,
                     window, &MainWindow::setIndexingProgress);
    QObject::connect(controller, &ChatController::statusChanged,
                     window, &MainWindow::setStatusText);
    QObject::connect(controller, &ChatController::backendSummaryReady,
                     window, &MainWindow::setBackendSummary);
    QObject::connect(controller, &ChatController::diagnosticsReady,
                     window, &MainWindow::setDiagnostics);
    QObject::connect(controller, &ChatController::sourceInventoryReady,
                     window, &MainWindow::setSourceInventory);
    QObject::connect(controller, &ChatController::backendModelsReady,
                     window, &MainWindow::setAvailableModels);
    QObject::connect(controller, &ChatController::desktopNotificationRequested,
                     notifications, &NotificationCenter::notify);

    window->setExternalSearchEnabledDefault(config.enableExternalSearch);

    for (const QString &bootstrapMessage : bootstrapMessages) {
        if (!bootstrapMessage.isEmpty()) {
            window->appendSystemMessage(bootstrapMessage);
        }
    }
    if (!configPathMessage.isEmpty()) {
        window->appendSystemMessage(configPathMessage);
    }
    if (!configLoadMessage.isEmpty()) {
        window->appendSystemMessage(configLoadMessage);
    }

    QObject::connect(&app, &QGuiApplication::lastWindowClosed,
                     &app, &QCoreApplication::quit);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [controller, notifications, window]() {
        notifications->shutdown();
        controller->prepareForShutdown();
        window->hide();
    });

    bootstrap.appendLog(QStringLiteral("Showing main window..."));
    window->show();
    bootstrap.close();
    return app.exec();
}
