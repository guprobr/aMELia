#include "ui/knowledgetreewidget.h"

#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QSet>
#include <QTreeWidgetItem>

#include <functional>

KnowledgeTreeWidget::KnowledgeTreeWidget(QWidget *parent)
    : QTreeWidget(parent)
{
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::CopyAction);
    setDragDropOverwriteMode(false);
}

QStringList KnowledgeTreeWidget::mimeTypes() const
{
    return {QString::fromLatin1(kKnowledgeMoveMimeType)};
}

QMimeData *KnowledgeTreeWidget::mimeData(const QList<QTreeWidgetItem *> &items) const
{
    QStringList paths;
    QSet<QString> seen;

    std::function<void(const QTreeWidgetItem *)> collectRecursive = [&](const QTreeWidgetItem *item) {
        if (item == nullptr) {
            return;
        }

        const QString directPath = item->data(0, kKnowledgePathRole).toString().trimmed();
        if (!directPath.isEmpty()) {
            if (!seen.contains(directPath)) {
                seen.insert(directPath);
                paths << directPath;
            }
            return;
        }

        for (int i = 0; i < item->childCount(); ++i) {
            collectRecursive(item->child(i));
        }
    };

    for (const QTreeWidgetItem *item : items) {
        collectRecursive(item);
    }

    if (paths.isEmpty()) {
        return nullptr;
    }

    auto *mimeData = new QMimeData();
    mimeData->setData(QString::fromLatin1(kKnowledgeMoveMimeType), paths.join(QLatin1Char('\n')).toUtf8());
    return mimeData;
}

Qt::DropActions KnowledgeTreeWidget::supportedDropActions() const
{
    return Qt::CopyAction;
}

void KnowledgeTreeWidget::dragMoveEvent(QDragMoveEvent *event)
{
    if (resolveDropTarget(event->position().toPoint()).first.trimmed().isEmpty()) {
        event->ignore();
        return;
    }
    if (!event->mimeData()->hasFormat(QString::fromLatin1(kKnowledgeMoveMimeType))) {
        event->ignore();
        return;
    }
    event->setDropAction(Qt::CopyAction);
    event->accept();
}

void KnowledgeTreeWidget::dropEvent(QDropEvent *event)
{
    if (!event->mimeData()->hasFormat(QString::fromLatin1(kKnowledgeMoveMimeType))) {
        event->ignore();
        return;
    }

    const auto target = resolveDropTarget(event->position().toPoint());
    const QString targetCollectionId = target.first.trimmed();
    if (targetCollectionId.isEmpty()) {
        event->ignore();
        return;
    }

    const QString payload = QString::fromUtf8(event->mimeData()->data(QString::fromLatin1(kKnowledgeMoveMimeType)));
    QStringList paths;
    QSet<QString> seen;
    for (const QString &line : payload.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty() && !seen.contains(trimmed)) {
            seen.insert(trimmed);
            paths << trimmed;
        }
    }
    if (paths.isEmpty()) {
        event->ignore();
        return;
    }

    emit knowledgeAssetsDropped(paths, targetCollectionId, target.second);
    event->setDropAction(Qt::CopyAction);
    event->accept();
}

QPair<QString, QString> KnowledgeTreeWidget::resolveDropTarget(const QPoint &pos) const
{
    QTreeWidgetItem *targetItem = itemAt(pos);
    if (targetItem == nullptr) {
        return {};
    }

    QString nodeType = targetItem->data(0, kKnowledgeNodeTypeRole).toString().trimmed();
    if (nodeType == QStringLiteral("file")) {
        targetItem = targetItem->parent();
        if (targetItem == nullptr) {
            return {};
        }
        nodeType = targetItem->data(0, kKnowledgeNodeTypeRole).toString().trimmed();
    }

    const QString collectionId = targetItem->data(0, kKnowledgeCollectionIdRole).toString().trimmed();
    if (collectionId.isEmpty()) {
        return {};
    }

    QString targetGroupLabel;
    if (nodeType == QStringLiteral("group")) {
        targetGroupLabel = targetItem->data(0, kKnowledgeGroupLabelRole).toString().trimmed();
    }
    return {collectionId, targetGroupLabel};
}
