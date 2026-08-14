/*
    SPDX-FileCopyrightText: 2026 KMail Vim Navigation contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <PimCommonAkonadi/GenericPluginInterface>

#include <Akonadi/Item>

#include <QKeySequence>
#include <QPointer>

class KActionCollection;
class QAction;
class VimMessageManager;

class VimNavigationPluginInterface final : public PimCommon::GenericPluginInterface
{
    Q_OBJECT

public:
    explicit VimNavigationPluginInterface(QObject *parent = nullptr);
    ~VimNavigationPluginInterface() override;

    void createAction(KActionCollection *actionCollection) override;
    void exec() override;
    void setItems(const Akonadi::Item::List &items) override;
    [[nodiscard]] RequireTypes requiresFeatures() const override;
    void updateActions(int numberOfSelectedItems, int numberOfSelectedCollections) override;

private:
    enum class PendingCommand {
        None,
        ToggleSelected,
        TagDeleted,
        TagArchived,
        TagSpam,
        UndoTag,
        ApplyTags,
    };

    QAction *createPluginAction(KActionCollection *actionCollection,
                                const QString &name,
                                const QString &text,
                                const QString &iconName,
                                const QKeySequence &shortcut);
    void activateCommand(PendingCommand command);
    void applyShortcuts();
    [[nodiscard]] QList<Akonadi::Item::Id> currentListItemIds() const;
    void scrollMessage(bool down);
    void refreshActionStates();

    QPointer<KActionCollection> mActionCollection;
    QPointer<QAction> mSelectedAction;
    QPointer<QAction> mDeletedAction;
    QPointer<QAction> mArchivedAction;
    QPointer<QAction> mSpamAction;
    QPointer<QAction> mUndoAction;
    QPointer<QAction> mApplyAction;
    Akonadi::Item::List mItems;
    VimMessageManager *const mMessageManager;
    PendingCommand mPendingCommand = PendingCommand::None;
};
