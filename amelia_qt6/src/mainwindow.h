#pragma once

#include <QMainWindow>
#include <QString>
#include <QStringList>

class QAction;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTextEdit;
class QTimer;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    void appendUserMessage(const QString &text);
    void appendAssistantChunk(const QString &text);
    void finalizeAssistantMessage(const QString &text);
    void appendSystemMessage(const QString &text);
    void setPrivacyPreview(const QString &text);
    void setLocalSources(const QString &text);
    void setExternalSources(const QString &text);
    void setOutlinePlan(const QString &text);
    void setMemoriesView(const QString &text);
    void setSessionSummary(const QString &text);
    void setTranscript(const QString &text);
    void setConversationList(const QStringList &ids,
                             const QStringList &titles,
                             const QString &currentId);
    void setStatusText(const QString &text);
    void setBusy(bool busy);
    void setIndexingActive(bool active);
    void setIndexingProgress(int value, int maximum, const QString &label);
    void setBackendSummary(const QString &text);
    void setDiagnostics(const QString &text);
    void setSourceInventory(const QString &text);
    void setAvailableModels(const QStringList &models, const QString &currentModel);
    void setExternalSearchEnabledDefault(bool enabled);

signals:
    void promptSubmitted(const QString &prompt, bool allowExternalSearch);
    void stopRequested();
    void reindexRequested();
    void testBackendRequested();
    void refreshModelsRequested();
    void newConversationRequested();
    void conversationSelected(const QString &conversationId);
    void rememberRequested(const QString &text);
    void backendModelSelected(const QString &model);
    void importPathsRequested(const QStringList &paths);
    void clearMemoriesRequested();

private slots:
    void onSendClicked();
    void onConversationItemChanged(QListWidgetItem *current, QListWidgetItem *previous);
    void onRememberClicked();
    void onImportFilesClicked();
    void onImportFolderClicked();
    void onModelSelectionChanged(const QString &model);
    void updateBusyIndicator();
    void showAboutAmelia();
    void showAboutQtDialog();
    void onClearMemoriesTriggered();
    void onPromptLabGenerateClicked();
    void onPromptLabUseClicked();
    void onPromptLabImportAssetsClicked();
    void onPromptLabBrowseFilesClicked();
    void onPromptLabBrowseFolderClicked();
    void onPromptLabCopyRecipeClicked();
    void onCopyLastAnswerClicked();
    void onCopyCodeBlocksClicked();

private:
    void appendTranscriptEntry(const QString &role, const QString &text);
    void appendDiagnosticEntry(const QString &timestamp, const QString &category, const QString &message);
    void rebuildTranscriptFromPlainText(const QString &text);
    void rebuildDiagnosticsFromPlainText(const QString &text);
    QString buildPromptLabRecipe() const;
    void insertTranscriptMessage(const QString &role, const QString &text);

    QTextEdit *m_transcript = nullptr;
    QPlainTextEdit *m_input = nullptr;
    QPlainTextEdit *m_privacyPreview = nullptr;
    QPlainTextEdit *m_localSources = nullptr;
    QPlainTextEdit *m_externalSources = nullptr;
    QPlainTextEdit *m_outlinePlan = nullptr;
    QPlainTextEdit *m_backendSummary = nullptr;
    QPlainTextEdit *m_memoriesView = nullptr;
    QPlainTextEdit *m_sessionSummary = nullptr;
    QTextEdit *m_diagnostics = nullptr;
    QPlainTextEdit *m_sourceInventory = nullptr;
    QListWidget *m_conversationsList = nullptr;
    QCheckBox *m_externalSearchCheck = nullptr;
    QComboBox *m_modelCombo = nullptr;
    QComboBox *m_promptLabPresetCombo = nullptr;
    QLineEdit *m_promptLabGoal = nullptr;
    QLineEdit *m_promptLabAssets = nullptr;
    QLineEdit *m_promptLabKbAssets = nullptr;
    QTextEdit *m_promptLabNotes = nullptr;
    QTextEdit *m_promptLabPreview = nullptr;
    QPushButton *m_sendButton = nullptr;
    QPushButton *m_stopButton = nullptr;
    QPushButton *m_reindexButton = nullptr;
    QPushButton *m_testBackendButton = nullptr;
    QPushButton *m_refreshModelsButton = nullptr;
    QPushButton *m_newConversationButton = nullptr;
    QPushButton *m_rememberButton = nullptr;
    QPushButton *m_importFilesButton = nullptr;
    QPushButton *m_importFolderButton = nullptr;
    QPushButton *m_promptLabGenerateButton = nullptr;
    QPushButton *m_promptLabUseButton = nullptr;
    QPushButton *m_promptLabImportButton = nullptr;
    QPushButton *m_promptLabBrowseFilesButton = nullptr;
    QPushButton *m_promptLabBrowseFolderButton = nullptr;
    QPushButton *m_promptLabCopyRecipeButton = nullptr;
    QPushButton *m_copyLastAnswerButton = nullptr;
    QPushButton *m_copyCodeBlocksButton = nullptr;
    QLabel *m_statusLabel = nullptr;
    QProgressBar *m_taskProgressBar = nullptr;
    QLabel *m_busyIndicatorLabel = nullptr;
    QTimer *m_busyIndicatorTimer = nullptr;
    QStringList m_busyFrames;
    int m_busyFrameIndex = 0;
    bool m_streamingAssistant = false;
    int m_streamingAssistantStartPosition = -1;
    QString m_lastAssistantMessage;
    bool m_indexingActive = false;
    bool m_updatingConversationList = false;
    bool m_updatingModelList = false;
    QAction *m_aboutAmeliaAction = nullptr;
    QAction *m_aboutQtAction = nullptr;
    QAction *m_clearMemoriesAction = nullptr;
};
