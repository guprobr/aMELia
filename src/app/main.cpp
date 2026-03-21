#include "core/appconfig.h"
#include "core/appversion.h"
#include "ui/bootstrapdialog.h"
#include "backend/chatcontroller.h"
#include "ui/mainwindow.h"
#include "ui/notificationcenter.h"

#include <QApplication>
#include <QCoreApplication>
#include <QEventLoop>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QIcon>
#include <QStringList>
#include <QTextStream>
#include <QTimer>

namespace {
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

QString promptForServiceUrl(QWidget *parent,
                            const QString &title,
                            const QString &label,
                            const QString &defaultValue,
                            QStringList &messages)
{
    bool accepted = false;
    const QString typedValue = QInputDialog::getText(parent,
                                                     title,
                                                     label,
                                                     QLineEdit::Normal,
                                                     defaultValue,
                                                     &accepted).trimmed();
    if (!accepted) {
        messages << QStringLiteral("%1 setup prompt canceled. Using default: %2").arg(title, defaultValue);
        return defaultValue;
    }
    if (typedValue.isEmpty()) {
        messages << QStringLiteral("%1 left blank. Using default: %2").arg(title, defaultValue);
        return defaultValue;
    }
    messages << QStringLiteral("%1 configured as %2").arg(title, typedValue);
    return typedValue;
}

QByteArray buildFirstRunConfigData(QWidget *parent, QStringList &messages)
{
    const QString ollamaBaseUrl = promptForServiceUrl(parent,
                                                      QStringLiteral("First-run Ollama setup"),
                                                      QStringLiteral("Ollama server URL:"),
                                                      ameliaDefaultOllamaBaseUrl(),
                                                      messages);
    const QString searxngUrl = promptForServiceUrl(parent,
                                                   QStringLiteral("First-run SearXNG setup"),
                                                   QStringLiteral("SearXNG search URL:"),
                                                   ameliaDefaultSearxngUrl(),
                                                   messages);
    return ameliaBuiltInDefaultConfigJson(ollamaBaseUrl, searxngUrl).toUtf8();
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

void ensureUserConfigSeeded(QStringList &messages, QWidget *promptParent = nullptr)
{
    const QString configPath = ameliaPreferredUserConfigPath();
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

    const QByteArray defaultConfigData = buildFirstRunConfigData(promptParent, messages);

    QString writeMessage;
    if (ensureFileWritten(configPath, defaultConfigData, &writeMessage)) {
        messages << QStringLiteral("Seeded user config at %1 from first-run setup")
                        .arg(QDir::toNativeSeparators(configPath));
    }
}
}

static QString resolveConfigPath(QString *message)
{
    const QString current = QDir::currentPath();
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString homeConfig = ameliaPreferredUserConfigPath();

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
    QApplication::setDesktopFileName(QStringLiteral("amelia_qt6"));
    QApplication::setQuitOnLastWindowClosed(true);

    auto *bootstrap = new BootstrapDialog();
    bootstrap->show();
    bootstrap->raise();
    bootstrap->activateWindow();
    bootstrap->repaint();
    bootstrap->setStatusText(QStringLiteral("Preparing Amelia runtime..."));
    app.processEvents(QEventLoop::AllEvents, 100);

    QStringList bootstrapMessages;
    ensureUserDataRoot(bootstrapMessages);
    ensureUserConfigSeeded(bootstrapMessages, bootstrap);
    for (const QString &message : bootstrapMessages) {
        bootstrap->appendLog(message);
    }

    QString configPathMessage;
    const QString configPath = resolveConfigPath(&configPathMessage);
    if (!configPathMessage.isEmpty()) {
        bootstrap->appendLog(configPathMessage);
    }

    QString configLoadMessage;
    const AppConfig config = AppConfigLoader::load(configPath, &configLoadMessage);
    if (!configLoadMessage.isEmpty()) {
        bootstrap->appendLog(configLoadMessage);
    }

    QTimer::singleShot(0, &app, [&, config, bootstrapMessages, configPathMessage, configLoadMessage]() {
        bootstrap->setStatusText(QStringLiteral("Building Amelia interface..."));
        bootstrap->appendLog(QStringLiteral("Creating main window..."));

        auto *window = new MainWindow(configPath, ameliaBuiltInDefaultConfigJson());
        auto *notifications = new NotificationCenter(config, &app);
        notifications->setAlertWidget(window);
        auto *controller = new ChatController(config);

        QObject::connect(controller, &ChatController::statusChanged,
                         bootstrap, &BootstrapDialog::setStatusText);
        QObject::connect(controller, &ChatController::systemNotice,
                         bootstrap, &BootstrapDialog::appendLog);

        QObject::connect(window, &MainWindow::promptSubmitted,
                         controller, &ChatController::sendUserPrompt);
        QObject::connect(window, &MainWindow::stopRequested,
                         controller, &ChatController::stopGeneration);
        QObject::connect(window, &MainWindow::reindexRequested,
                         controller, &ChatController::reindexDocs);
        QObject::connect(window, &MainWindow::cancelIndexingRequested,
                         controller, &ChatController::cancelReindex);
        QObject::connect(window, &MainWindow::testBackendRequested,
                         controller, &ChatController::probeBackend);
        QObject::connect(window, &MainWindow::refreshModelsRequested,
                         controller, &ChatController::refreshBackendModels);
        QObject::connect(window, &MainWindow::newConversationRequested,
                         controller, &ChatController::newConversation);
        QObject::connect(window, &MainWindow::conversationSelected,
                         controller, &ChatController::loadConversationById);
        QObject::connect(window, &MainWindow::deleteConversationRequested,
                         controller, &ChatController::deleteConversationById);
        QObject::connect(window, &MainWindow::reasoningTraceCaptureToggled,
                         controller, &ChatController::setReasoningTraceEnabled);
        QObject::connect(window, &MainWindow::prioritizedKnowledgeAssetsChanged,
                         controller, &ChatController::setPrioritizedKnowledgeAssets);
        QObject::connect(window, &MainWindow::rememberRequested,
                         controller, &ChatController::rememberNote);
        QObject::connect(window, &MainWindow::backendModelSelected,
                         controller, &ChatController::setBackendModel);
        QObject::connect(window, &MainWindow::importPathsRequested,
                         controller, &ChatController::importKnowledgePaths);
        QObject::connect(window, &MainWindow::addPathsToKnowledgeCollectionRequested,
                         controller, &ChatController::addKnowledgePathsToCollection);
        QObject::connect(window, &MainWindow::createKnowledgeCollectionRequested,
                         controller, &ChatController::createKnowledgeCollection);
        QObject::connect(window, &MainWindow::deleteKnowledgeCollectionRequested,
                         controller, &ChatController::deleteKnowledgeCollection);
        QObject::connect(window, &MainWindow::renameKnowledgeAssetRequested,
                         controller, &ChatController::renameKnowledgeAsset);
        QObject::connect(window, &MainWindow::renameKnowledgeCollectionRequested,
                         controller, &ChatController::renameKnowledgeCollection);
        QObject::connect(window, &MainWindow::clearMemoriesRequested,
                         controller, &ChatController::clearMemories);
        QObject::connect(window, &MainWindow::removeKnowledgeAssetsRequested,
                         controller, &ChatController::removeKnowledgeAssets);
        QObject::connect(window, &MainWindow::moveKnowledgeAssetsRequested,
                         controller, &ChatController::moveKnowledgeAssets);
        QObject::connect(window, &MainWindow::clearKnowledgeBaseRequested,
                         controller, &ChatController::clearKnowledgeBase);

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
        QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [controller, notifications, window, bootstrap]() {
            notifications->shutdown();
            controller->prepareForShutdown();
            window->hide();
            bootstrap->hide();
        });

        QObject::connect(controller, &ChatController::startupFinished, &app, [window, bootstrap]() {
            bootstrap->appendLog(QStringLiteral("Showing main window..."));
            window->show();
            bootstrap->close();
        });

        bootstrap->appendLog(QStringLiteral("Controller created. Starting asynchronous bootstrap..."));
        controller->startBootstrap();
    });

    return app.exec();
}
