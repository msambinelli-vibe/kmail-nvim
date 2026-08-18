/*
    SPDX-FileCopyrightText: 2026 KMail Vim Navigation contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <MailCommon/SearchRule>

#include <Akonadi/Item>

#include <QString>

#include <memory>

namespace KMime
{
class Message;
}

namespace QuickFilter
{
enum class ConditionKind {
    ListId,
    Sender,
    SenderDomain,
    Subject,
};

struct Condition {
    ConditionKind kind = ConditionKind::Subject;
    QByteArray field;
    MailCommon::SearchRule::Function function = MailCommon::SearchRule::FuncContains;
    QString value;
    bool enabled = false;
};

enum class WorkflowAction {
    Deleted,
    Spam,
    Archived,
};

enum class ExistingMessages {
    CurrentFolder,
    CurrentMessage,
    FutureOnly,
};

[[nodiscard]] QList<Condition> conditionsFromMessage(const std::shared_ptr<KMime::Message> &message);
[[nodiscard]] QString conditionLabel(const Condition &condition);
[[nodiscard]] QString searchContents(const Condition &condition);
[[nodiscard]] QString workflowTagName(WorkflowAction action);
[[nodiscard]] QString workflowActionLabel(WorkflowAction action);
[[nodiscard]] bool matches(const QList<Condition> &conditions, const Akonadi::Item &item);
[[nodiscard]] QString filterBaseName(const QList<Condition> &conditions, WorkflowAction action);
}
