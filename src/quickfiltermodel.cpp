/*
    SPDX-FileCopyrightText: 2026 KMail Vim Navigation contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "quickfiltermodel.h"

#include <MailCommon/SearchPattern>

#include <KMime/Message>

#include <QRegularExpression>

#include <algorithm>

namespace
{
QString subjectOf(const std::shared_ptr<KMime::Message> &message)
{
    const auto *header = message ? message->subject() : nullptr;
    return header ? header->asUnicodeString().trimmed() : QString();
}

QString senderOf(const std::shared_ptr<KMime::Message> &message)
{
    const auto *header = message ? message->from() : nullptr;
    if (!header) {
        return {};
    }
    const auto mailboxes = header->mailboxes();
    if (mailboxes.isEmpty()) {
        return {};
    }
    return QString::fromUtf8(mailboxes.constFirst().address()).trimmed().toLower();
}

QString listIdOf(const std::shared_ptr<KMime::Message> &message)
{
    const auto *header = message ? message->headerByType("List-Id") : nullptr;
    if (!header) {
        return {};
    }

    const QString raw = header->asUnicodeString().trimmed();
    static const QRegularExpression canonicalId(QStringLiteral("<[^<>\\s]+>"));
    const QRegularExpressionMatch match = canonicalId.match(raw);
    return match.hasMatch() ? match.captured() : raw;
}

QString senderDomainPattern(const QString &domain)
{
    return QStringLiteral("@%1(?=[>\\s,;]|$)").arg(QRegularExpression::escape(domain));
}
}

QList<QuickFilter::Condition> QuickFilter::conditionsFromMessage(const std::shared_ptr<KMime::Message> &message)
{
    QList<Condition> result;
    const QString listId = listIdOf(message);
    const QString sender = senderOf(message);
    const QString subject = subjectOf(message);

    if (!listId.isEmpty()) {
        result.push_back({ConditionKind::ListId,
                          QByteArrayLiteral("List-Id"),
                          MailCommon::SearchRule::FuncContains,
                          listId,
                          true});
    }
    if (!sender.isEmpty()) {
        result.push_back({ConditionKind::Sender,
                          QByteArrayLiteral("From"),
                          MailCommon::SearchRule::FuncContains,
                          sender,
                          listId.isEmpty()});

        const qsizetype separator = sender.lastIndexOf(QLatin1Char('@'));
        const QString domain = separator >= 0 ? sender.mid(separator + 1).trimmed() : QString();
        if (!domain.isEmpty()) {
            result.push_back({ConditionKind::SenderDomain,
                              QByteArrayLiteral("From"),
                              MailCommon::SearchRule::FuncRegExp,
                              domain,
                              false});
        }
    }
    if (!subject.isEmpty()) {
        result.push_back({ConditionKind::Subject,
                          QByteArrayLiteral("Subject"),
                          MailCommon::SearchRule::FuncContains,
                          subject,
                          false});
    }
    return result;
}

QString QuickFilter::conditionLabel(const Condition &condition)
{
    switch (condition.kind) {
    case ConditionKind::ListId:
        return QObject::tr("List-Id contém %1").arg(condition.value);
    case ConditionKind::Sender:
        return QObject::tr("From contém %1").arg(condition.value);
    case ConditionKind::SenderDomain:
        return QObject::tr("Domínio do remetente: %1").arg(condition.value);
    case ConditionKind::Subject:
        return QObject::tr("Assunto contém “%1”").arg(condition.value);
    }
    return {};
}

QString QuickFilter::searchContents(const Condition &condition)
{
    return condition.kind == ConditionKind::SenderDomain
        ? senderDomainPattern(condition.value.trimmed().toLower())
        : condition.value.trimmed();
}

QString QuickFilter::workflowTagName(WorkflowAction action)
{
    switch (action) {
    case WorkflowAction::Deleted:
        return QStringLiteral("deleted");
    case WorkflowAction::Spam:
        return QStringLiteral("spam");
    case WorkflowAction::Archived:
        return QStringLiteral("archived");
    }
    return {};
}

QString QuickFilter::workflowActionLabel(WorkflowAction action)
{
    switch (action) {
    case WorkflowAction::Deleted:
        return QObject::tr("Exclusão");
    case WorkflowAction::Spam:
        return QObject::tr("Spam");
    case WorkflowAction::Archived:
        return QObject::tr("Arquivamento");
    }
    return {};
}

bool QuickFilter::matches(const QList<Condition> &conditions, const Akonadi::Item &item)
{
    MailCommon::SearchPattern pattern;
    pattern.setOp(MailCommon::SearchPattern::OpAnd);
    for (const Condition &condition : conditions) {
        if (!condition.enabled || condition.value.trimmed().isEmpty()) {
            continue;
        }
        pattern.append(MailCommon::SearchRule::createInstance(condition.field, condition.function, searchContents(condition)));
    }
    return !pattern.isEmpty() && pattern.matches(item);
}

QString QuickFilter::filterBaseName(const QList<Condition> &conditions, WorkflowAction action)
{
    const auto firstEnabled = std::find_if(conditions.cbegin(), conditions.cend(), [](const Condition &condition) {
        return condition.enabled && !condition.value.trimmed().isEmpty();
    });
    const QString source = firstEnabled == conditions.cend() ? QObject::tr("mensagens") : firstEnabled->value.trimmed();
    return QObject::tr("Rápido: %1 → %2").arg(source.left(60), workflowTagName(action));
}
