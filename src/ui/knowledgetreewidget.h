#pragma once

#include <QTreeWidget>

// Custom Qt::UserRole+N data roles MainWindow stashes on Knowledge Base tree items to
// track each node's identity (file path, owning collection, node type) without a
// separate parallel model. Shared between KnowledgeTreeWidget (which reads them to
// resolve drag/drop targets) and MainWindow (which writes them when populating the
// tree).
constexpr int kKnowledgeNodeTypeRole = Qt::UserRole + 20;
constexpr int kKnowledgePathRole = Qt::UserRole + 21;
constexpr int kKnowledgeCollectionIdRole = Qt::UserRole + 22;
constexpr int kKnowledgeSearchBlobRole = Qt::UserRole + 23;
constexpr int kKnowledgeGroupLabelRole = Qt::UserRole + 24;
constexpr int kKnowledgePropertiesRole = Qt::UserRole + 25;
constexpr char kKnowledgeMoveMimeType[] = "application/x-amelia-kb-paths";

// Knowledge Base inventory tree: supports both dropping external files/folders onto it
// (handled by MainWindow) and dragging items within the tree to move them between
// collections/groups (handled here via a custom internal MIME type, resolved to a
// target collection/group by walking up to the nearest collection or group node under
// the drop point).
class KnowledgeTreeWidget final : public QTreeWidget {
    Q_OBJECT
public:
    explicit KnowledgeTreeWidget(QWidget *parent = nullptr);

signals:
    void knowledgeAssetsDropped(const QStringList &paths,
                                const QString &targetCollectionId,
                                const QString &targetGroupLabel);

protected:
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QList<QTreeWidgetItem *> &items) const override;
    Qt::DropActions supportedDropActions() const override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    QPair<QString, QString> resolveDropTarget(const QPoint &pos) const;
};
