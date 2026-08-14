/*
    SPDX-FileCopyrightText: 2026 KMail Vim Navigation contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "vimnavigationplugininterface.h"

#include "messagelistmodelutils.h"
#include "vimmessagemanager.h"
#include "vimshortcutmapper.h"

#include <MessageList/Pane>
#include <MessageViewer/Viewer>
#include <PimCommon/BroadcastStatus>

#include <KActionCollection>

#include <QAction>
#include <QDebug>
#include <QIcon>
#include <QItemSelectionModel>
#include <QKeyCombination>
#include <QKeySequence>
#include <QTimer>
#include <QTreeView>
#include <QWidget>

#include <algorithm>

VimNavigationPluginInterface::VimNavigationPluginInterface(QObject *parent)
    : PimCommon::GenericPluginInterface(parent)
    , mMessageManager(new VimMessageManager(this))
{
    connect(mMessageManager, &VimMessageManager::stateChanged, this, &VimNavigationPluginInterface::refreshActionStates);
    connect(mMessageManager, &VimMessageManager::statusMessage, this, [this](const QString &status) {
        PimCommon::BroadcastStatus::instance()->setTransientStatusMsg(status);
        Q_EMIT message(status);
    });
    connect(mMessageManager, &VimMessageManager::tagDisplayChanged, this, [this] {
        QTimer::singleShot(0, this, [this] {
            if (QWidget *const widget = parentWidget()) {
                if (auto *const pane = widget->findChild<MessageList::Pane *>()) {
                    pane->updateTagComboBox();
                }
            }
        });
    });
}

VimNavigationPluginInterface::~VimNavigationPluginInterface() = default;

void VimNavigationPluginInterface::createAction(KActionCollection *actionCollection)
{
    mActionCollection = actionCollection;

    auto *scrollDown = createPluginAction(actionCollection,
                                          QStringLiteral("vim_scroll_message_down"),
                                          tr("Rolar mensagem uma página para baixo"),
                                          QStringLiteral("go-down"),
                                          QKeySequence(QKeyCombination(Qt::ShiftModifier, Qt::Key_J)));
    connect(scrollDown, &QAction::triggered, this, [this] {
        scrollMessage(true);
    });

    auto *scrollUp = createPluginAction(actionCollection,
                                        QStringLiteral("vim_scroll_message_up"),
                                        tr("Rolar mensagem uma página para cima"),
                                        QStringLiteral("go-up"),
                                        QKeySequence(QKeyCombination(Qt::ShiftModifier, Qt::Key_K)));
    connect(scrollUp, &QAction::triggered, this, [this] {
        scrollMessage(false);
    });

    mSelectedAction = createPluginAction(actionCollection,
                                         QStringLiteral("vim_toggle_selected"),
                                         tr("Alternar seleção persistente"),
                                         QStringLiteral("mail-tagged"),
                                         QKeySequence(Qt::Key_Space));
    connect(mSelectedAction, &QAction::triggered, this, [this] {
        activateCommand(PendingCommand::ToggleSelected);
    });

    mDeletedAction = createPluginAction(actionCollection,
                                        QStringLiteral("vim_tag_deleted"),
                                        tr("Marcar para excluir"),
                                        QStringLiteral("edit-delete"),
                                        QKeySequence(QKeyCombination(Qt::NoModifier, Qt::Key_D),
                                                     QKeyCombination(Qt::NoModifier, Qt::Key_D)));
    connect(mDeletedAction, &QAction::triggered, this, [this] {
        activateCommand(PendingCommand::TagDeleted);
    });

    mUndoAction = createPluginAction(actionCollection,
                                     QStringLiteral("vim_undo_tag"),
                                     tr("Desfazer última atribuição de tag"),
                                     QStringLiteral("edit-undo"),
                                     QKeySequence(Qt::Key_U));
    connect(mUndoAction, &QAction::triggered, this, [this] {
        activateCommand(PendingCommand::UndoTag);
    });

    mArchivedAction = createPluginAction(actionCollection,
                                         QStringLiteral("vim_tag_archived"),
                                         tr("Marcar para arquivar"),
                                         QStringLiteral("mail-archive"),
                                         QKeySequence(Qt::Key_A));
    connect(mArchivedAction, &QAction::triggered, this, [this] {
        activateCommand(PendingCommand::TagArchived);
    });

    mSpamAction = createPluginAction(actionCollection,
                                     QStringLiteral("vim_tag_spam"),
                                     tr("Marcar como spam pendente"),
                                     QStringLiteral("mail-mark-junk"),
                                     QKeySequence(Qt::Key_S));
    connect(mSpamAction, &QAction::triggered, this, [this] {
        activateCommand(PendingCommand::TagSpam);
    });

    mApplyAction = createPluginAction(actionCollection,
                                      QStringLiteral("vim_apply_tags"),
                                      tr("Aplicar ações das tags"),
                                      QStringLiteral("dialog-ok-apply"),
                                      QKeySequence(QKeyCombination(Qt::ShiftModifier, Qt::Key_S)));
    connect(mApplyAction, &QAction::triggered, this, [this] {
        activateCommand(PendingCommand::ApplyTags);
    });

    refreshActionStates();
    mMessageManager->ensureRequiredTags();

    // KMail creates generic plugin interfaces just before it creates its own
    // navigation actions. Defer the lookup until the main widget is complete.
    QTimer::singleShot(0, this, &VimNavigationPluginInterface::applyShortcuts);
}

void VimNavigationPluginInterface::exec()
{
    const PendingCommand command = mPendingCommand;
    mPendingCommand = PendingCommand::None;

    switch (command) {
    case PendingCommand::ToggleSelected:
        mMessageManager->toggleSelectedTag(mItems);
        break;
    case PendingCommand::TagDeleted:
        mMessageManager->assignWorkflowTag(QStringLiteral("deleted"), mItems, currentListItemIds());
        break;
    case PendingCommand::TagArchived:
        mMessageManager->assignWorkflowTag(QStringLiteral("archived"), mItems, currentListItemIds());
        break;
    case PendingCommand::TagSpam:
        mMessageManager->assignWorkflowTag(QStringLiteral("spam"), mItems, currentListItemIds());
        break;
    case PendingCommand::UndoTag:
        mMessageManager->undoLastTagAssignment();
        break;
    case PendingCommand::ApplyTags:
        mMessageManager->applyTaggedActions(mItems);
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
                                                          const QString &iconName,
                                                          const QKeySequence &shortcut)
{
    QAction *const action = actionCollection->addAction(name);
    action->setText(text);
    action->setIcon(QIcon::fromTheme(iconName));
    KActionCollection::setDefaultShortcut(action, shortcut);
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
