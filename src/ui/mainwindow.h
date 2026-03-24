#pragma once

#include <QHash>
#include <QJsonObject>
#include <QMainWindow>
#include <QCloseEvent>
#include <QString>
#include <QStringList>

class QAction;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QTreeWidget;
class QTreeWidgetItem;
class QPlainTextEdit;
class QPoint;
class QProgressBar;
class QPushButton;
class QStackedLayout;
class QTextEdit;
class QTimer;
class QUrl;
class QWidget;
class QScrollBar;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const QString &configPath = QString(),
                        const QString &defaultConfigJson = QString(),
                        QWidget *parent = nullptr);

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
    void cancelIndexingRequested();
    void testBackendRequested();
    void refreshModelsRequested();
    void newConversationRequested();
    void conversationSelected(const QString &conversationId);
    void rememberRequested(const QString &text);
    void deleteMemoryRequested(const QString &memoryId);
    void backendModelSelected(const QString &model);
    void importPathsRequested(const QStringList &paths, const QString &label);
    void addPathsToKnowledgeCollectionRequested(const QStringList &paths, const QString &collectionId);
    void createKnowledgeCollectionRequested(const QString &label);
    void deleteKnowledgeCollectionRequested(const QString &collectionId);
    void clearMemoriesRequested();
    void removeKnowledgeAssetsRequested(const QStringList &paths);
    void moveKnowledgeAssetsRequested(const QStringList &paths, const QString &targetCollectionId, const QString &targetGroupLabel);
    void renameKnowledgeAssetRequested(const QString &path, const QString &newFileName);
    void clearKnowledgeBaseRequested();
    void deleteConversationRequested(const QString &conversationId);
    void reasoningTraceCaptureToggled(bool enabled);
    void verboseDiagnosticsToggled(bool enabled);
    void prioritizedKnowledgeAssetsChanged(const QStringList &paths);
    void renameKnowledgeCollectionRequested(const QString &collectionId, const QString &newLabel);

private slots:
    void onSendClicked();
    void onConversationItemChanged(QListWidgetItem *current, QListWidgetItem *previous);
    void onRememberClicked();
    void onImportFilesClicked();
    void onImportFolderClicked();
    void onReindexClicked();
    void onModelSelectionChanged(const QString &model);
    void updateBusyIndicator();
    void showAboutAmelia();
    void showAboutQtDialog();
    void onClearMemoriesTriggered();
    void onDeleteSelectedMemoryClicked();
    void onPromptLabGenerateClicked();
    void onPromptLabUseClicked();
    void onPromptLabImportAssetsClicked();
    void onPromptLabBrowseFilesClicked();
    void onPromptLabBrowseFolderClicked();
    void onPromptLabCopyRecipeClicked();
    void onCopyLastAnswerClicked();
    void onCopyCodeBlocksClicked();
    void onKnowledgeBaseFilterTextChanged(const QString &text);
    void onKnowledgeBaseSortModeChanged(int index);
    void onRemoveSelectedKnowledgeAssetsClicked();
    void onClearKnowledgeBaseClicked();
    void onTranscriptAnchorClicked(const QUrl &url);
    void onDeleteConversationClicked();
    void onReasoningTraceToggleToggled(bool checked);
    void onVerboseDiagnosticsToggleToggled(bool checked);
    void onPrioritizeSelectedKnowledgeAssetsClicked();
    void onPinSelectedKnowledgeAssetsClicked();
    void onRemoveSelectedPrioritizedAssetsClicked();
    void onClearPrioritizedAssetsClicked();
    void onRenameSelectedKnowledgeGroupClicked();
    void onKnowledgeAssetsDropped(const QStringList &paths, const QString &targetCollectionId, const QString &targetGroupLabel);
    void onKnowledgeTreeContextMenuRequested(const QPoint &pos);
    void onOpenConfigurationDialog();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void appendTranscriptEntry(const QString &role, const QString &text);
    void appendDiagnosticEntry(const QString &timestamp, const QString &category, const QString &message);
    void rebuildTranscriptFromPlainText(const QString &text);
    void rebuildDiagnosticsFromPlainText(const QString &text);
    QString buildPromptLabRecipe() const;
    void insertTranscriptMessage(const QString &role, const QString &text);
    void applyKnowledgeBaseFilter();
    void updateKnowledgeBaseFilterStatus();
    QStringList selectedKnowledgeAssetPaths() const;
    QStringList collectKnowledgePathsFromItem(const QTreeWidgetItem *item) const;
    QTreeWidgetItem *collectionItemForKnowledgeItem(QTreeWidgetItem *item) const;
    QString displayLabelForKnowledgePath(const QString &path) const;
    QString promptForKnowledgeLabel(const QString &title, const QString &suggestedLabel) const;
    void rebuildPrioritizedKnowledgeAssetsUi();
    void emitPrioritizedKnowledgeAssetsState();
    QTreeWidgetItem *knowledgeContextItem(const QPoint *treePos = nullptr) const;
    void showKnowledgePropertiesDialog(const QString &title, const QJsonObject &properties, const QString &nodeType);
    bool confirmKnowledgeBaseReindexAction(const QString &action, const QString &details = QString()) const;
    void setKnowledgeInventoryRefreshVisible(bool visible, const QString &label = QString());
    void updateSelectedMemoryDetails();
    void beginResponseProgress(const QString &label = QString());
    void setResponseProgressStage(int value, const QString &label);
    void setResponseProgressBusy(const QString &label);
    void updateResponseStreamingProgress(const QString &chunk);
    void finishResponseProgress(const QString &label = QString());
    void cancelResponseProgress(const QString &label = QString());
    void resetTaskProgressBar();
    void updateKnowledgeBaseControlsEnabled();
    bool saveConfigurationJson(const QString &text, QString *errorMessage = nullptr);

    QTextEdit *m_transcript = nullptr;
    QPlainTextEdit *m_input = nullptr;
    QListWidget *m_prioritizedAssetsList = nullptr;
    QLabel *m_prioritizedAssetsStatus = nullptr;
    QPlainTextEdit *m_privacyPreview = nullptr;
    QPlainTextEdit *m_localSources = nullptr;
    QPlainTextEdit *m_externalSources = nullptr;
    QPlainTextEdit *m_outlinePlan = nullptr;
    QPlainTextEdit *m_backendSummary = nullptr;
    QTreeWidget *m_memoriesView = nullptr;
    QPlainTextEdit *m_memoryDetails = nullptr;
    QPlainTextEdit *m_sessionSummary = nullptr;
    QTextEdit *m_diagnostics = nullptr;
    QPlainTextEdit *m_sourceInventory = nullptr;
    QPlainTextEdit *m_sourceInventoryStats = nullptr;
    QLineEdit *m_sourceInventoryFilter = nullptr;
    QLabel *m_sourceInventoryFilterStatus = nullptr;
    QTreeWidget *m_sourceInventoryTree = nullptr;
    QStackedLayout *m_sourceInventoryStack = nullptr;
    QLabel *m_sourceInventoryRefreshLabel = nullptr;
    QListWidget *m_conversationsList = nullptr;
    QPushButton *m_deleteConversationButton = nullptr;
    QCheckBox *m_externalSearchCheck = nullptr;
    QComboBox *m_modelCombo = nullptr;
    QComboBox *m_promptLabPresetCombo = nullptr;
    QComboBox *m_sourceInventorySortCombo = nullptr;
    QLineEdit *m_promptLabGoal = nullptr;
    QLineEdit *m_promptLabAssets = nullptr;
    QLineEdit *m_promptLabKbAssets = nullptr;
    QTextEdit *m_promptLabNotes = nullptr;
    QTextEdit *m_promptLabPreview = nullptr;
    QPushButton *m_sendButton = nullptr;
    QPushButton *m_stopButton = nullptr;
    QPushButton *m_reindexButton = nullptr;
    QPushButton *m_cancelIndexingButton = nullptr;
    QPushButton *m_testBackendButton = nullptr;
    QPushButton *m_refreshModelsButton = nullptr;
    QPushButton *m_newConversationButton = nullptr;
    QPushButton *m_rememberButton = nullptr;
    QPushButton *m_deleteMemoryButton = nullptr;
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
    QPushButton *m_reasoningTraceToggleButton = nullptr;
    QPushButton *m_verboseDiagnosticsToggleButton = nullptr;
    QLabel *m_reasoningTraceInfoLabel = nullptr;
    QLabel *m_verboseDiagnosticsInfoLabel = nullptr;
    QPushButton *m_prioritizeSelectedAssetButton = nullptr;
    QPushButton *m_pinSelectedAssetButton = nullptr;
    QPushButton *m_removePrioritizedAssetButton = nullptr;
    QPushButton *m_clearPrioritizedAssetsButton = nullptr;
    QPushButton *m_removeSelectedAssetButton = nullptr;
    QPushButton *m_clearKnowledgeBaseButton = nullptr;
    QPushButton *m_renameKnowledgeLabelButton = nullptr;
    QLabel *m_statusLabel = nullptr;
    QProgressBar *m_taskProgressBar = nullptr;
    QLabel *m_busyIndicatorLabel = nullptr;
    QTimer *m_busyIndicatorTimer = nullptr;
    QStringList m_busyFrames;
    int m_busyFrameIndex = 0;
    bool m_streamingAssistant = false;
    int m_streamingAssistantStartPosition = -1;
    QString m_lastAssistantMessage;
    bool m_responseProgressActive = false;
    bool m_responseFirstTokenReceived = false;
    int m_responseProgressValue = 0;
    int m_streamReceivedChars = 0;
    int m_streamEstimatedChars = 1400;
    bool m_indexingActive = false;
    bool m_closePendingAfterIndexCancel = false;
    bool m_knowledgeInventoryRefreshVisible = false;
    QString m_knowledgeInventoryRefreshBaseLabel;
    bool m_updatingConversationList = false;
    bool m_updatingModelList = false;
    bool m_transcriptAutoScroll = true;
    QAction *m_newConversationAction = nullptr;
    QAction *m_aboutAmeliaAction = nullptr;
    QAction *m_configurationAction = nullptr;
    QAction *m_aboutQtAction = nullptr;
    QAction *m_clearMemoriesAction = nullptr;
    QString m_configPath;
    QString m_defaultConfigJson;
    QStringList m_oneShotPrioritizedAssets;
    QStringList m_pinnedKnowledgeAssets;
    QStringList m_transcriptCodeBlocks;
    QStringList m_transcriptAssistantAnswers;
    QHash<QString, QString> m_knowledgeDisplayNames;
};
