/*
    SPDX-FileCopyrightText: 2026 KMail Vim Navigation contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "quickfiltermodel.h"

#include <Akonadi/Collection>
#include <Akonadi/Item>
#include <Akonadi/Tag>

#include <QObject>
#include <QPointer>

class QuickFilterDialog;
class VimMessageManager;
class QWidget;

namespace MailCommon
{
class FilterManager;
class MailFilter;
}

class QuickFilterController final : public QObject
{
    Q_OBJECT

public:
    explicit QuickFilterController(VimMessageManager *messageManager, QObject *parent = nullptr);

    void open(Akonadi::Item::Id currentItemId, const Akonadi::Collection &currentFolder, QWidget *parentWidget);
    [[nodiscard]] bool isOpen() const;

Q_SIGNALS:
    void statusMessage(const QString &message);
    void stateChanged();

private:
    struct PendingDraft {
        QList<QuickFilter::Condition> conditions;
        QuickFilter::WorkflowAction action = QuickFilter::WorkflowAction::Deleted;
        QuickFilter::ExistingMessages existingMessages = QuickFilter::ExistingMessages::FutureOnly;
    };

    void fetchFolderMessages(const Akonadi::Collection &folder);
    void updatePreview();
    void finishRequested();
    void waitForFilterBackend(const PendingDraft &draft, const Akonadi::Tag &tag);
    void saveFilter(const PendingDraft &draft, const Akonadi::Tag &tag, MailCommon::FilterManager *manager);
    void applyToExistingMessages(const PendingDraft &draft, const QString &tagName, const QString &filterName);
    [[nodiscard]] MailCommon::MailFilter *buildFilter(const PendingDraft &draft,
                                                      const Akonadi::Tag &tag,
                                                      MailCommon::FilterManager *manager,
                                                      QString *error) const;
    [[nodiscard]] bool nativeFilterDialogIsOpen() const;
    [[nodiscard]] QString previewRow(const Akonadi::Item &item) const;
    void fail(const QString &error);
    void reset();

    VimMessageManager *const mMessageManager;
    QPointer<QuickFilterDialog> mDialog;
    Akonadi::Item mSourceItem;
    Akonadi::Collection mCurrentFolder;
    Akonadi::Item::List mFolderItems;
    Akonadi::Item::List mMatchingItems;
    QString mPreviewError;
    bool mFolderFetchFinished = false;
    bool mSaving = false;
    bool mOpening = false;
    quint64 mGeneration = 0;
};
