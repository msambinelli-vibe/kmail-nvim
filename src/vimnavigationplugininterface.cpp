/*
    SPDX-FileCopyrightText: 2026 KMail Vim Navigation contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "vimnavigationplugininterface.h"

#include "messagelistmodelutils.h"
#include "vimmessagemanager.h"
#include "vimshortcutmapper.h"

#include <Akonadi/EntityTreeModel>

#include <MessageList/Pane>
#include <MessageViewer/Viewer>
#include <PimCommon/BroadcastStatus>

#include <KActionCollection>

#include <QAction>
#include <QDebug>
#include <QIcon>
#include <QItemSelectionModel>
#include <QTimer>
#include <QTreeView>
#include <QWidget>

#include <algorithm>
#include <utility>

VimNavigationPluginInterface::VimNavigationPluginInterface(QObject *parent)
    : PimCommon::GenericPluginInterface(parent)
    , mMessageManager(new VimMessageManager(this))
{
    connect(mMessageManager, &VimMessageManager::stateChanged, this, &VimNavigationPluginInterface::refreshActionStates);
    connect(mMessageManager, &VimMessageManager::statusMessage, this, [this](const QString &status) {
        PimCommon::BroadcastStatus::instance()->setTransientStatusMsg(status);
        Q_EMIT message(status);
    });
    connect(mMessageManager, &VimMessageManager::selectedTagChangeFinished, this, [this](bool success) {
        const qint64 originFolderId = std::exchange(mSelectedToggleFolderId, -1);
        const Akonadi::Item::Id originItemId = std::exchange(mSelectedToggleItemId, -1);
        if (!success) {
            return;
        }

        const Akonadi::Collection folder = currentFolder();
        if (originFolderId > 0 && (!folder.isValid() || folder.id() != originFolderId)) {
            return;
        }
        if (originItemId > 0 && currentMessageId() != originItemId) {
            return;
        }
        advanceToNextMessage();
    });
}

VimNavigationPluginInterface::~VimNavigationPluginInterface() = default;

void VimNavigationPluginInterface::createAction(KActionCollection *actionCollection)
{
    mActionCollection = actionCollection;

    auto *scrollDown = createPluginAction(actionCollection,
                                          QStringLiteral("vim_scroll_message_down"),
                                          tr("Rolar mensagem uma página para baixo"),
                                          QStringLiteral("go-down"));
    connect(scrollDown, &QAction::triggered, this, [this] {
        scrollMessage(true);
    });

    auto *scrollUp = createPluginAction(actionCollection,
                                        QStringLiteral("vim_scroll_message_up"),
                                        tr("Rolar mensagem uma página para cima"),
                                        QStringLiteral("go-up"));
    connect(scrollUp, &QAction::triggered, this, [this] {
        scrollMessage(false);
    });

    mSelectedAction = createPluginAction(actionCollection,
                                         QStringLiteral("vim_toggle_selected"),
                                         tr("Alternar seleção persistente"),
                                         QStringLiteral("mail-tagged"));
    connect(mSelectedAction, &QAction::triggered, this, [this] {
        activateCommand(PendingCommand::ToggleSelected);
    });

    mDeletedAction = createPluginAction(actionCollection,
                                        QStringLiteral("vim_tag_deleted"),
                                        tr("Alternar marcação para excluir"),
                                        QStringLiteral("edit-delete"));
    connect(mDeletedAction, &QAction::triggered, this, [this] {
        activateCommand(PendingCommand::TagDeleted);
    });

    mUndoAction = createPluginAction(actionCollection,
                                     QStringLiteral("vim_undo_tag"),
                                     tr("Desfazer última alternância de tag"),
                                     QStringLiteral("edit-undo"));
    connect(mUndoAction, &QAction::triggered, this, [this] {
        activateCommand(PendingCommand::UndoTag);
    });

    mArchivedAction = createPluginAction(actionCollection,
                                         QStringLiteral("vim_tag_archived"),
                                         tr("Alternar marcação para arquivar"),
                                         QStringLiteral("mail-archive"));
    connect(mArchivedAction, &QAction::triggered, this, [this] {
        activateCommand(PendingCommand::TagArchived);
    });

    mSpamAction = createPluginAction(actionCollection,
                                     QStringLiteral("vim_tag_spam"),
                                     tr("Alternar marcação como spam pendente"),
                                     QStringLiteral("mail-mark-junk"));
    connect(mSpamAction, &QAction::triggered, this, [this] {
        activateCommand(PendingCommand::TagSpam);
    });

    mClearSelectedAction = createPluginAction(actionCollection,
                                              QStringLiteral("vim_clear_selected"),
                                              tr("Limpar seleção persistente da lista"),
                                              QStringLiteral("edit-clear"));
    connect(mClearSelectedAction, &QAction::triggered, this, [this] {
        activateCommand(PendingCommand::ClearSelected);
    });

    mClearAllTagsAction = createPluginAction(actionCollection,
                                             QStringLiteral("vim_clear_all_tags"),
                                             tr("Limpar todas as tags do plugin na lista"),
                                             QStringLiteral("edit-clear-all"));
    connect(mClearAllTagsAction, &QAction::triggered, this, [this] {
        activateCommand(PendingCommand::ClearAllTags);
    });

    mApplyAction = createPluginAction(actionCollection,
                                      QStringLiteral("vim_apply_tags"),
                                      tr("Aplicar ações das tags na pasta atual"),
                                      QStringLiteral("dialog-ok-apply"));
    connect(mApplyAction, &QAction::triggered, this, [this] {
        activateCommand(PendingCommand::ApplyTags);
    });

    refreshActionStates();
    mMessageManager->ensureRequiredTags();

    // KMail creates generic plugin interfaces before its built-in actions and
    // creates tag/filter actions later during startup. Re-apply the reservation
    // after every insertion so those late actions cannot retain a Vim key.
    connect(actionCollection, &KActionCollection::inserted, this, [this, actionCollection](QAction *) {
        if (mActionCollection == actionCollection) {
            // KActionCollection emits inserted() before KMail calls
            // setDefaultShortcut(). Release the plugin targets synchronously,
            // then restore them after the new action is fully configured.
            VimShortcutMapper::releaseAll(actionCollection);
            scheduleShortcutUpdate();
        }
    });
    scheduleShortcutUpdate();
}

void VimNavigationPluginInterface::exec()
{
    const PendingCommand command = mPendingCommand;
    mPendingCommand = PendingCommand::None;

    switch (command) {
    case PendingCommand::ToggleSelected:
        mSelectedToggleFolderId = currentFolder().id();
        mSelectedToggleItemId = currentMessageId();
        mMessageManager->toggleSelectedTag(mItems);
        break;
    case PendingCommand::TagDeleted:
        mMessageManager->toggleWorkflowTag(QStringLiteral("deleted"), mItems, currentListItemIds());
        break;
    case PendingCommand::TagArchived:
        mMessageManager->toggleWorkflowTag(QStringLiteral("archived"), mItems, currentListItemIds());
        break;
    case PendingCommand::TagSpam:
        mMessageManager->toggleWorkflowTag(QStringLiteral("spam"), mItems, currentListItemIds());
        break;
    case PendingCommand::ClearSelected:
        mMessageManager->clearTagsFromList({QStringLiteral("selected")}, currentListItemIds());
        break;
    case PendingCommand::ClearAllTags:
        mMessageManager->clearTagsFromList({QStringLiteral("selected"),
                                            QStringLiteral("archived"),
                                            QStringLiteral("deleted"),
                                            QStringLiteral("spam")},
                                           currentListItemIds());
        break;
    case PendingCommand::UndoTag:
        mMessageManager->undoLastTagAssignment();
        break;
    case PendingCommand::ApplyTags:
        mMessageManager->applyTaggedActions(currentFolder());
        break;
    case PendingCommand::None:
        break;
    }
}

void VimNavigationPluginInterface::setItems(const Akonadi::Item::List &items)
{
    mItems = items;
}

PimCommon::GenericPluginInterface::RequireTypes VimNavigationPluginInterface::requiresFeatures() const
{
    return CurrentItems;
}

void VimNavigationPluginInterface::updateActions(int numberOfSelectedItems, int)
{
    Q_UNUSED(numberOfSelectedItems)
    refreshActionStates();
}

QAction *VimNavigationPluginInterface::createPluginAction(KActionCollection *actionCollection,
                                                          const QString &name,
                                                          const QString &text,
                                                          const QString &iconName)
{
    QAction *const action = actionCollection->addAction(name);
    action->setText(text);
    action->setIcon(QIcon::fromTheme(iconName));
    addActionType(PimCommon::ActionType(action, PimCommon::ActionType::Message));
    return action;
}

void VimNavigationPluginInterface::activateCommand(PendingCommand command)
{
    if (mMessageManager->isBusy()) {
        return;
    }
    mPendingCommand = command;
    Q_EMIT emitPluginActivated(this);
}

void VimNavigationPluginInterface::scrollMessage(bool down)
{
    QWidget *const widget = parentWidget();
    auto *const viewer = widget ? widget->findChild<MessageViewer::Viewer *>() : nullptr;
    if (!viewer) {
        Q_EMIT message(tr("Nenhuma mensagem está aberta para rolagem."));
        return;
    }

    if (down) {
        viewer->slotScrollNext();
    } else {
        viewer->slotScrollPrior();
    }
}

void VimNavigationPluginInterface::advanceToNextMessage()
{
    QAction *const nextMessage = mActionCollection ? mActionCollection->action(QStringLiteral("go_next_message")) : nullptr;
    if (nextMessage && nextMessage->isEnabled()) {
        nextMessage->trigger();
    }
}

QList<Akonadi::Item::Id> VimNavigationPluginInterface::currentListItemIds() const
{
    QWidget *const widget = parentWidget();
    auto *const pane = widget ? widget->findChild<MessageList::Pane *>() : nullptr;
    QWidget *const currentTab = pane ? pane->currentWidget() : nullptr;
    QItemSelectionModel *const selectionModel = pane ? pane->currentItemSelectionModel() : nullptr;
    // MessageList::Core::View is a QTreeView, but its static meta-object is not
    // exported by all MessageList builds. Looking it up through the public base
    // type keeps the plugin loadable across those builds.
    if (currentTab && selectionModel) {
        const auto treeViews = currentTab->findChildren<QTreeView *>();
        const auto matchingView = std::find_if(treeViews.cbegin(), treeViews.cend(), [selectionModel](const QTreeView *candidate) {
            return candidate->selectionModel() == selectionModel;
        });
        if (matchingView != treeViews.cend()) {
            return MessageListModelUtils::visibleItemIds(*matchingView);
        }
    }
    return {};
}

Akonadi::Item::Id VimNavigationPluginInterface::currentMessageId() const
{
    QWidget *const widget = parentWidget();
    auto *const pane = widget ? widget->findChild<MessageList::Pane *>() : nullptr;
    QItemSelectionModel *const selectionModel = pane ? pane->currentItemSelectionModel() : nullptr;
    if (!selectionModel) {
        return -1;
    }

    const QModelIndex currentIndex = selectionModel->currentIndex();
    if (!currentIndex.isValid()) {
        return -1;
    }
    const Akonadi::Item::Id id = currentIndex.data(Akonadi::EntityTreeModel::ItemIdRole).toLongLong();
    return id > 0 ? id : -1;
}

Akonadi::Collection VimNavigationPluginInterface::currentFolder() const
{
    QWidget *const widget = parentWidget();
    auto *const pane = widget ? widget->findChild<MessageList::Pane *>() : nullptr;
    return pane ? pane->currentFolder() : Akonadi::Collection();
}

void VimNavigationPluginInterface::refreshActionStates()
{
    const bool idle = !mMessageManager->isBusy();
    if (mSelectedAction) {
        mSelectedAction->setEnabled(idle);
    }
    if (mDeletedAction) {
        mDeletedAction->setEnabled(idle);
    }
    if (mArchivedAction) {
        mArchivedAction->setEnabled(idle);
    }
    if (mSpamAction) {
        mSpamAction->setEnabled(idle);
    }
    if (mClearSelectedAction) {
        mClearSelectedAction->setEnabled(idle);
    }
    if (mClearAllTagsAction) {
        mClearAllTagsAction->setEnabled(idle);
    }
    if (mApplyAction) {
        mApplyAction->setEnabled(idle);
    }
    if (mUndoAction) {
        mUndoAction->setEnabled(idle && mMessageManager->canUndo());
    }
}

void VimNavigationPluginInterface::applyShortcuts()
{
    if (!mActionCollection) {
        return;
    }

    if (!VimShortcutMapper::apply(mActionCollection)) {
        qWarning() << "KMail Vim Navigation: navigation actions were not found";
    }
}

void VimNavigationPluginInterface::scheduleShortcutUpdate()
{
    if (mShortcutUpdateScheduled) {
        return;
    }

    mShortcutUpdateScheduled = true;
    QTimer::singleShot(0, this, [this] {
        mShortcutUpdateScheduled = false;
        applyShortcuts();
    });
}
