/*
    SPDX-FileCopyrightText: 2026 KMail Vim Navigation contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <Akonadi/Item>
#include <Akonadi/Tag>

#include <QObject>
#include <QHash>
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

    void ensureRequiredTags();
    void toggleSelectedTag(const Akonadi::Item::List &items);
    void assignWorkflowTag(const QString &tagName,
                           const Akonadi::Item::List &fallbackItems,
                           const QList<Akonadi::Item::Id> &currentListItemIds);
    void undoLastTagAssignment();
    void applyTaggedActions(const Akonadi::Collection &collection);

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
    void finishRequiredTagInitialization();
    void completeRequiredTagInitialization(const QString &tagName, const Akonadi::Tag &tag, const QString &error);
    void resolveTag(const QString &tagName, TagCallback callback);
    void prepareTagForKMail(const QString &tagName, const Akonadi::Tag &tag, TagCallback callback);
    void createRequiredTag(const QString &tagName, TagCallback callback);
    void fetchItemsWithTags(const Akonadi::Item::List &items, FetchCallback callback);
    void fetchItemsWithTagName(const QString &tagName, FetchCallback callback);
    void setTagState(const Akonadi::Tag &tag, const Akonadi::Item::List &items, bool present, FetchCallback callback);
    bool registerTagForDisplay(const Akonadi::Tag &tag, QString *error);
    void addFallbackSelection(const Akonadi::Tag &selectedTag,
                              const Akonadi::Item::List &fallbackItems,
                              const QString &workflowTagName);
    void assignWorkflowTagToItems(const QString &tagName,
                                  const Akonadi::Tag &selectedTag,
                                  const Akonadi::Item::List &items);
    void clearSelectionAfterAssignment(const QString &tagName,
                                       const Akonadi::Tag &selectedTag,
                                       const Akonadi::Item::List &items,
                                       int assignedCount,
                                       bool alreadyTagged);

    void startDeleteAction(const Akonadi::Item::List &items);
    void startSpamAction(const Akonadi::Item::List &items);
    void startArchiveActions(const Akonadi::Item::List &items);
    void archiveGroups(const QList<ArchiveGroup> &groups, qsizetype index, QStringList errors, ErrorCallback callback);
    void resolveArchiveDestination(const QString &resource, CollectionCallback callback);
    void clearWorkflowTags(const Akonadi::Item::List &items, ErrorCallback callback);
    void finishApplyOperation(const QString &error = {});

    bool mBusy = false;
    bool mTagInitializationInProgress = false;
    int mTagInitializationAttempts = 0;
    int mPendingTagInitializations = 0;
    QStringList mTagInitializationErrors;
    QHash<QString, Akonadi::Tag> mTags;
    Akonadi::Tag mUndoTag;
    Akonadi::Item::List mUndoItems;
    int mPendingApplyOperations = 0;
    int mApplyMessageCount = 0;
    QStringList mApplyErrors;
};
