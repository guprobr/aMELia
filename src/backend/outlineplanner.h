#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

// Shared high-level retrieval intent used by planning and ranking.
enum class RetrievalIntent {
    General,
    Troubleshooting,
    DocumentGeneration,
    Architecture,
    Implementation
};

QString retrievalIntentName(RetrievalIntent intent);

struct OutlineSectionPlan {
    QString title;
    QString objective;
    QString query;
    QStringList preferredRoles;
};

struct OutlinePlan {
    bool enabled = false;
    QString documentType;
    RetrievalIntent intent = RetrievalIntent::General;
    QString rationale;
    QString overview;
    QVector<OutlineSectionPlan> sections;

    QString formatForPrompt() const;
    QString formatForUi() const;
};

class OutlinePlanner {
public:
    OutlinePlan planForPrompt(const QString &prompt) const;

private:
    QStringList extractAnchorTerms(const QString &prompt) const;
    QVector<OutlineSectionPlan> buildMopSections(const QStringList &anchors) const;
    QVector<OutlineSectionPlan> buildRunbookSections(const QStringList &anchors) const;
    QVector<OutlineSectionPlan> buildGuideSections(const QStringList &anchors) const;
};
