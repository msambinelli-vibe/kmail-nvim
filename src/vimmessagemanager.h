/*
    SPDX-FileCopyrightText: 2026 KMail Vim Navigation contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <Akonadi/Item>
#include <Akonadi/Tag>

#include <QObject>
#include <QStringList>

#include <functional>

namespace Akonadi
{
class Collection;
}

class VimMessageManager final : public QObject
{
    Q_OBJECT

public:
    explicit VimMessageManager(QObject *parent = nullptr);

    [[nodiscard]] bool isBusy() const;
    [[nodiscard]] bool canUndo() const;

    void assignTag(const QString &tagName, const Akonadi::Item::List &items);
    void undoLastTagAssignment();
    void applyTaggedActions(const Akonadi::Item::List &items);

Q_SIGNALS:
    void stateChanged();
    void statusMessage(const QString &message);

private:
    struct ArchiveGroup {
        QString resource;
        Akonadi::Item::List items;
    };

    using CollectionCallback = std::function<void(const Akonadi::Collection &, const QString &)>;
    using ErrorCallback = std::function<void(const QString &)>;
    using FetchCallback = std::function<void(const Akonadi::Item::List &, const QString &)>;
    using TagCallback = std::function<void(const Akonadi::Tag &, const QString &)>;

    void setBusy(bool busy);
    void finishCommand(const QString &message);
    void resolveTag(const QString &tagName, TagCallback callback);
    void fetchItemsWithTags(const Akonadi::Item::List &items, FetchCallback callback);

    void startDeleteAction(const Akonadi::Item::List &items);
    void startSpamAction(const Akonadi::Item::List &items);
    void startArchiveActions(const Akonadi::Item::List &items);
    void archiveGroups(const QList<ArchiveGroup> &groups, qsizetype index, QStringList errors, ErrorCallback callback);
    void resolveArchiveDestination(const QString &resource, CollectionCallback callback);
    void clearWorkflowTags(const Akonadi::Item::List &items, ErrorCallback callback);
    void finishApplyOperation(const QString &error = {});

    bool mBusy = false;
    Akonadi::Tag mUndoTag;
    Akonadi::Item::List mUndoItems;
    int mPendingApplyOperations = 0;
    int mApplyMessageCount = 0;
    QStringList mApplyErrors;
};

