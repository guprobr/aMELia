#include "outlineplanner.h"

#include <QRegularExpression>
#include <QSet>

namespace {
QString joinAnchors(const QStringList &anchors)
{
    return anchors.isEmpty() ? QString() : anchors.join(QStringLiteral(" "));
}

QStringList defaultStopWords()
{
    return {
        QStringLiteral("the"), QStringLiteral("and"), QStringLiteral("with"), QStringLiteral("from"),
        QStringLiteral("into"), QStringLiteral("that"), QStringLiteral("this"), QStringLiteral("your"),
        QStringLiteral("what"), QStringLiteral("when"), QStringLiteral("where"), QStringLiteral("which"),
        QStringLiteral("will"), QStringLiteral("would"), QStringLiteral("should"), QStringLiteral("about"),
        QStringLiteral("through"), QStringLiteral("whole"), QStringLiteral("format"), QStringLiteral("make"),
        QStringLiteral("create"), QStringLiteral("write"), QStringLiteral("need"), QStringLiteral("using"),
        QStringLiteral("para"), QStringLiteral("como"), QStringLiteral("isso"), QStringLiteral("essa"),
        QStringLiteral("esse"), QStringLiteral("uma"), QStringLiteral("com"), QStringLiteral("sem"),
        QStringLiteral("dos"), QStringLiteral("das"), QStringLiteral("from"), QStringLiteral("bare"),
        QStringLiteral("metal")
    };
}
}

QString retrievalIntentName(RetrievalIntent intent)
{
    switch (intent) {
    case RetrievalIntent::Troubleshooting:
        return QStringLiteral("troubleshooting");
    case RetrievalIntent::DocumentGeneration:
        return QStringLiteral("document_generation");
    case RetrievalIntent::Architecture:
        return QStringLiteral("architecture");
    case RetrievalIntent::Implementation:
        return QStringLiteral("implementation");
    case RetrievalIntent::General:
    default:
        return QStringLiteral("general");
    }
}

QString OutlinePlan::formatForPrompt() const
{
    if (!enabled || sections.isEmpty()) {
        return QString();
    }

    QStringList lines;
    lines << QStringLiteral("Document type: %1").arg(documentType.isEmpty() ? QStringLiteral("structured_document") : documentType);
    lines << QStringLiteral("Intent: %1").arg(retrievalIntentName(intent));
    if (!rationale.trimmed().isEmpty()) {
        lines << QStringLiteral("Why this plan exists: %1").arg(rationale);
    }
    if (!overview.trimmed().isEmpty()) {
        lines << QStringLiteral("Planner overview: %1").arg(overview);
    }
    lines << QStringLiteral("Use the following outline and retrieved section evidence to write a coherent answer.");
    for (int i = 0; i < sections.size(); ++i) {
        const OutlineSectionPlan &section = sections.at(i);
        lines << QStringLiteral("%1. %2").arg(i + 1).arg(section.title);
        lines << QStringLiteral("   objective: %1").arg(section.objective);
        if (!section.preferredRoles.isEmpty()) {
            lines << QStringLiteral("   preferred_sources: %1").arg(section.preferredRoles.join(QStringLiteral(", ")));
        }
    }
    return lines.join(QStringLiteral("\n"));
}

QString OutlinePlan::formatForUi() const
{
    if (!enabled || sections.isEmpty()) {
        return QStringLiteral("<none>");
    }

    QStringList lines;
    lines << QStringLiteral("Document type: %1").arg(documentType.isEmpty() ? QStringLiteral("structured_document") : documentType);
    lines << QStringLiteral("Intent: %1").arg(retrievalIntentName(intent));
    if (!overview.trimmed().isEmpty()) {
        lines << QStringLiteral("Overview: %1").arg(overview);
    }
    if (!rationale.trimmed().isEmpty()) {
        lines << QStringLiteral("Rationale: %1").arg(rationale);
    }
    lines << QString();

    for (int i = 0; i < sections.size(); ++i) {
        const OutlineSectionPlan &section = sections.at(i);
        lines << QStringLiteral("%1. %2").arg(i + 1).arg(section.title);
        lines << QStringLiteral("   Objective: %1").arg(section.objective);
        lines << QStringLiteral("   Retrieval query: %1").arg(section.query);
        if (!section.preferredRoles.isEmpty()) {
            lines << QStringLiteral("   Preferred roles: %1").arg(section.preferredRoles.join(QStringLiteral(", ")));
        }
        lines << QString();
    }

    return lines.join(QStringLiteral("\n"));
}

OutlinePlan OutlinePlanner::planForPrompt(const QString &prompt) const
{
    OutlinePlan plan;
    const QString lower = prompt.toLower();

    const bool looksLikeTroubleshooting = lower.contains(QStringLiteral("error"))
            || lower.contains(QStringLiteral("failed"))
            || lower.contains(QStringLiteral("debug"))
            || lower.contains(QStringLiteral("alarm"))
            || lower.contains(QStringLiteral("troubleshoot"));
    const bool looksLikeArchitecture = lower.contains(QStringLiteral("hld"))
            || lower.contains(QStringLiteral("lld"))
            || lower.contains(QStringLiteral("topology"))
            || lower.contains(QStringLiteral("architecture"))
            || lower.contains(QStringLiteral("design"));
    const bool looksLikeDoc = lower.contains(QStringLiteral("mop"))
            || lower.contains(QStringLiteral("runbook"))
            || lower.contains(QStringLiteral("playbook"))
            || lower.contains(QStringLiteral("guide"))
            || lower.contains(QStringLiteral("procedure"))
            || lower.contains(QStringLiteral("markdown"))
            || lower.contains(QStringLiteral(".md"));
    const bool looksLikeImplementation = lower.contains(QStringLiteral("deploy"))
            || lower.contains(QStringLiteral("bootstrap"))
            || lower.contains(QStringLiteral("install"))
            || lower.contains(QStringLiteral("bring-up"))
            || lower.contains(QStringLiteral("bring up"));

    if (!looksLikeDoc) {
        if (looksLikeTroubleshooting) {
            plan.intent = RetrievalIntent::Troubleshooting;
        } else if (looksLikeArchitecture) {
            plan.intent = RetrievalIntent::Architecture;
        } else if (looksLikeImplementation) {
            plan.intent = RetrievalIntent::Implementation;
        }
        return plan;
    }

    const QStringList anchors = extractAnchorTerms(prompt);
    plan.enabled = true;

    if (lower.contains(QStringLiteral("mop")) || lower.contains(QStringLiteral("method of procedure"))) {
        plan.documentType = QStringLiteral("MOP");
        plan.intent = RetrievalIntent::DocumentGeneration;
        plan.rationale = QStringLiteral("The request asks for an operational document spanning multiple deployment phases, so Amelia should plan first and retrieve evidence section by section.");
        plan.overview = QStringLiteral("Start with assumptions and inputs, then walk through preparation, deployment, validation and rollback.");
        plan.sections = buildMopSections(anchors);
    } else if (lower.contains(QStringLiteral("runbook")) || lower.contains(QStringLiteral("playbook"))) {
        plan.documentType = QStringLiteral("Runbook");
        plan.intent = RetrievalIntent::DocumentGeneration;
        plan.rationale = QStringLiteral("Runbooks work better when each operational phase gets its own retrieval query and source weighting.");
        plan.overview = QStringLiteral("Prioritize procedure and scenario documents, with reference docs only filling in missing commands and checks.");
        plan.sections = buildRunbookSections(anchors);
    } else {
        plan.documentType = QStringLiteral("Guide");
        plan.intent = looksLikeArchitecture ? RetrievalIntent::Architecture : RetrievalIntent::DocumentGeneration;
        plan.rationale = QStringLiteral("The prompt asks for a structured document, so Amelia builds an outline before writing the final answer.");
        plan.overview = QStringLiteral("Retrieve evidence per section instead of asking one large prompt to cover the entire corpus at once.");
        plan.sections = buildGuideSections(anchors);
    }

    return plan;
}

QStringList OutlinePlanner::extractAnchorTerms(const QString &prompt) const
{
    QString normalized = prompt.toLower();
    normalized.replace(QRegularExpression(QStringLiteral("[^a-z0-9.+_-]+")), QStringLiteral(" "));
    const QStringList parts = normalized.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const QStringList stopWordsList = defaultStopWords();

    QStringList anchors;
    QSet<QString> seen;
    for (const QString &part : parts) {
        if (part.size() < 3 || stopWordsList.contains(part) || seen.contains(part)) {
            continue;
        }
        if (part == QStringLiteral("controllers") || part == QStringLiteral("workers") || part == QStringLiteral("platform")
                || part == QStringLiteral("ciq") || part == QStringLiteral("idrac") || part == QStringLiteral("globalprotect")
                || part == QStringLiteral("dell") || part == QStringLiteral("poweredge") || part == QStringLiteral("central")
                || part == QStringLiteral("cluster") || part == QStringLiteral("cloud") || part == QStringLiteral("deploying")
                || part.contains(QStringLiteral("25.09")) || part.contains(QStringLiteral("24.09")) || part.contains(QStringLiteral("vpn"))) {
            anchors << part;
            seen.insert(part);
            continue;
        }
        if (anchors.size() < 10) {
            anchors << part;
            seen.insert(part);
        }
    }

    return anchors;
}

QVector<OutlineSectionPlan> OutlinePlanner::buildMopSections(const QStringList &anchors) const
{
    const QString tail = joinAnchors(anchors);
    return {
        {QStringLiteral("Scope and target topology"),
         QStringLiteral("Define the cluster shape, supported assumptions, node roles and deployment end-state."),
         QStringLiteral("scope target topology controllers workers central cloud standard cluster %1").arg(tail),
         {QStringLiteral("scenario"), QStringLiteral("procedure")}},
        {QStringLiteral("Assumptions, access and prerequisites"),
         QStringLiteral("Gather remote access assumptions, VPN requirements, iDRAC access, images, credentials and installation prerequisites."),
         QStringLiteral("prerequisites access vpn globalprotect idrac credentials hardware requirements %1").arg(tail),
         {QStringLiteral("scenario"), QStringLiteral("procedure"), QStringLiteral("reference")}},
        {QStringLiteral("CIQ inputs and planning gates"),
         QStringLiteral("Map the CIQ spreadsheet to required deployment inputs, addressing, hostnames, networks and validation before execution."),
         QStringLiteral("ciq spreadsheet addressing hostnames management oam network deployment inputs %1").arg(tail),
         {QStringLiteral("scenario"), QStringLiteral("procedure")}},
        {QStringLiteral("Bare-metal and host preparation"),
         QStringLiteral("Prepare DELL PowerEdge hosts, firmware assumptions, boot order, virtual media and installation staging."),
         QStringLiteral("dell poweredge bare metal idrac virtual media boot host preparation firmware %1").arg(tail),
         {QStringLiteral("procedure"), QStringLiteral("scenario"), QStringLiteral("reference")}},
        {QStringLiteral("Distributed platform deployment execution"),
         QStringLiteral("Describe the sequence from install media to controller and worker bring-up."),
         QStringLiteral("platform install bootstrap controller worker deployment bring up %1").arg(tail),
         {QStringLiteral("procedure"), QStringLiteral("reference")}},
        {QStringLiteral("Post-deployment validation"),
         QStringLiteral("List health checks, cluster validation gates and acceptance criteria."),
         QStringLiteral("validation health checks system host interface kubernetes cluster acceptance %1").arg(tail),
         {QStringLiteral("procedure"), QStringLiteral("reference")}},
        {QStringLiteral("Rollback and recovery points"),
         QStringLiteral("Identify safe stop points, rollback boundaries and recovery actions during deployment."),
         QStringLiteral("rollback recovery restart failure handling deployment %1").arg(tail),
         {QStringLiteral("procedure"), QStringLiteral("reference")}},
        {QStringLiteral("Appendix: commands, artifacts and evidence"),
         QStringLiteral("Capture command references, expected artifacts, and evidence to retain during the change."),
         QStringLiteral("commands artifacts evidence checklist runbook appendix %1").arg(tail),
         {QStringLiteral("reference"), QStringLiteral("procedure")}}
    };
}

QVector<OutlineSectionPlan> OutlinePlanner::buildRunbookSections(const QStringList &anchors) const
{
    const QString tail = joinAnchors(anchors);
    return {
        {QStringLiteral("Purpose and scope"), QStringLiteral("State what the runbook covers and when it applies."),
         QStringLiteral("purpose scope applicability %1").arg(tail), {QStringLiteral("scenario"), QStringLiteral("procedure")}},
        {QStringLiteral("Prerequisites"), QStringLiteral("Capture access, tools, credentials and environment assumptions."),
         QStringLiteral("prerequisites access tools credentials %1").arg(tail), {QStringLiteral("procedure"), QStringLiteral("reference")}},
        {QStringLiteral("Execution steps"), QStringLiteral("Document the sequence of operational actions."),
         QStringLiteral("execution steps procedure commands %1").arg(tail), {QStringLiteral("procedure"), QStringLiteral("reference")}},
        {QStringLiteral("Validation"), QStringLiteral("List checks that confirm the procedure succeeded."),
         QStringLiteral("validation checks expected result %1").arg(tail), {QStringLiteral("procedure"), QStringLiteral("reference")}},
        {QStringLiteral("Recovery"), QStringLiteral("Explain what to do when a step fails."),
         QStringLiteral("recovery failure handling rollback %1").arg(tail), {QStringLiteral("procedure"), QStringLiteral("reference")}}
    };
}

QVector<OutlineSectionPlan> OutlinePlanner::buildGuideSections(const QStringList &anchors) const
{
    const QString tail = joinAnchors(anchors);
    return {
        {QStringLiteral("Overview"), QStringLiteral("Explain the overall objective and environment."),
         QStringLiteral("overview objective environment %1").arg(tail), {QStringLiteral("scenario"), QStringLiteral("reference")}},
        {QStringLiteral("Assumptions"), QStringLiteral("Capture key assumptions and constraints."),
         QStringLiteral("assumptions constraints %1").arg(tail), {QStringLiteral("scenario")}},
        {QStringLiteral("Procedure"), QStringLiteral("Walk through the relevant steps in order."),
         QStringLiteral("procedure implementation steps %1").arg(tail), {QStringLiteral("procedure"), QStringLiteral("reference")}},
        {QStringLiteral("Validation"), QStringLiteral("Show how to verify the result."),
         QStringLiteral("validation verification checks %1").arg(tail), {QStringLiteral("procedure"), QStringLiteral("reference")}}
    };
}
