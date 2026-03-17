#pragma once

#include <QMainWindow>
#include <QString>
#include <QStringList>
#include <QVector>

class QAction;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;
class QPushButton;
class QTextBrowser;
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
    void onPromptLabBrowseAssetsClicked();
    void onPromptLabBrowseFolderClicked();
    void onPromptLabClearAssetsClicked();
    void onPromptLabCopyRecipeClicked();
    void onPromptLabAddSelectedKbAssetsClicked();
    void onPromptLabKbFilterChanged(const QString &text);
    void onCopyLastAnswerClicked();
    void onCopyTranscriptClicked();
    void onCopySelectedCodeBlockClicked();
    void onTranscriptContextMenuRequested(const QPoint &pos);

private:
    struct TranscriptEntry {
        QString role;
        QString text;
    };

    struct CodeBlockItem {
        QString label;
        QString code;
    };

    void appendTranscriptEntry(const QString &role, const QString &text);
    void appendDiagnosticEntry(const QString &timestamp, const QString &category, const QString &message);
    void rebuildTranscriptFromPlainText(const QString &text);
    void rebuildDiagnosticsFromPlainText(const QString &text);
    QString buildPromptLabRecipe() const;
    void renderTranscript();
    QString renderMessageBodyHtml(const QString &text,
                                  const QString &role,
                                  int assistantIndex,
                                  int &globalCodeIndex,
                                  int &perMessageCodeIndex);
    QString markdownFragmentToHtml(const QString &markdown) const;
    void refreshCodeBlockSelector();
    void refreshKbAssetList(const QString &filterText = QString());
    QStringList promptLabImportAssets() const;
    QStringList promptLabSelectedKbAssets() const;
    QStringList promptLabManualKbAssets() const;
    void copyTextToClipboard(const QString &text, const QString &statusMessage);

    QTextBrowser *m_transcript = nullptr;
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
    QPlainTextEdit *m_promptLabImportAssetsEdit = nullptr;
    QLineEdit *m_promptLabKbFilter = nullptr;
    QListWidget *m_promptLabKbAssetsList = nullptr;
    QLineEdit *m_promptLabKbManualEdit = nullptr;
    QTextEdit *m_promptLabNotes = nullptr;
    QTextEdit *m_promptLabPreview = nullptr;
    QComboBox *m_codeBlockCombo = nullptr;
    QPushButton *m_sendButton = nullptr;
    QPushButton *m_stopButton = nullptr;
    QPushButton *m_reindexButton = nullptr;
    QPushButton *m_testBackendButton = nullptr;
    QPushButton *m_refreshModelsButton = nullptr;
    QPushButton *m_newConversationButton = nullptr;
    QPushButton *m_rememberButton = nullptr;
    QPushButton *m_importFilesButton = nullptr;
    QPushButton *m_importFolderButton = nullptr;
    QPushButton *m_copyLastAnswerButton = nullptr;
    QPushButton *m_copyTranscriptButton = nullptr;
    QPushButton *m_copyCodeBlockButton = nullptr;
    QPushButton *m_promptLabGenerateButton = nullptr;
    QPushButton *m_promptLabUseButton = nullptr;
    QPushButton *m_promptLabImportButton = nullptr;
    QPushButton *m_promptLabBrowseAssetsButton = nullptr;
    QPushButton *m_promptLabBrowseFolderButton = nullptr;
    QPushButton *m_promptLabClearAssetsButton = nullptr;
    QPushButton *m_promptLabCopyRecipeButton = nullptr;
    QPushButton *m_promptLabAddSelectedKbButton = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_busyIndicatorLabel = nullptr;
    QTimer *m_busyIndicatorTimer = nullptr;
    QStringList m_busyFrames;
    QStringList m_knownKbAssets;
    QVector<TranscriptEntry> m_transcriptEntries;
    QVector<CodeBlockItem> m_codeBlocks;
    QString m_lastAssistantMessage;
    int m_busyFrameIndex = 0;
    bool m_streamingAssistant = false;
    bool m_updatingConversationList = false;
    bool m_updatingModelList = false;
    QAction *m_aboutAmeliaAction = nullptr;
    QAction *m_aboutQtAction = nullptr;
    QAction *m_clearMemoriesAction = nullptr;
};
