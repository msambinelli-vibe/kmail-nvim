/*
    SPDX-FileCopyrightText: 2026 KMail Vim Navigation contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "vimshortcutmapper.h"

#include <KActionCollection>

#include <QAction>
#include <QHash>
#include <QKeyCombination>
#include <QKeySequence>
#include <QList>
#include <QString>

namespace
{
const QKeySequence vimNextShortcut(Qt::Key_J);
const QKeySequence vimPreviousShortcut(Qt::Key_K);
const QKeySequence vimScrollDownShortcut(QKeyCombination(Qt::ShiftModifier, Qt::Key_J));
const QKeySequence vimScrollUpShortcut(QKeyCombination(Qt::ShiftModifier, Qt::Key_K));
const QKeySequence vimDeleteShortcut(QKeyCombination(Qt::NoModifier, Qt::Key_D), QKeyCombination(Qt::NoModifier, Qt::Key_D));
const QKeySequence vimUndoShortcut(Qt::Key_U);
const QKeySequence vimArchiveShortcut(Qt::Key_A);
const QKeySequence vimFirstShortcut(QKeyCombination(Qt::NoModifier, Qt::Key_G), QKeyCombination(Qt::NoModifier, Qt::Key_G));
const QKeySequence vimLastShortcut(QKeyCombination(Qt::ShiftModifier, Qt::Key_G));
const QKeySequence vimSpamShortcut(Qt::Key_S);
const QKeySequence vimApplyShortcut(QKeyCombination(Qt::ShiftModifier, Qt::Key_S));
const QKeySequence vimToggleSelectedShortcut(Qt::Key_Space);
const QKeySequence jumpToFolderFallback(QKeyCombination(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_J));

const QList<QKeySequence> &reservedShortcuts()
{
    static const QList<QKeySequence> shortcuts = {
        vimNextShortcut,
        vimPreviousShortcut,
        vimScrollDownShortcut,
        vimScrollUpShortcut,
        vimDeleteShortcut,
        vimUndoShortcut,
        vimArchiveShortcut,
        vimFirstShortcut,
        vimLastShortcut,
        vimSpamShortcut,
        vimApplyShortcut,
        vimToggleSelectedShortcut,
    };
    return shortcuts;
}

const QHash<QString, QKeySequence> &targetShortcuts()
{
    static const QHash<QString, QKeySequence> shortcuts = {
        {QStringLiteral("go_next_message"), vimNextShortcut},
        {QStringLiteral("go_prev_message"), vimPreviousShortcut},
        {QStringLiteral("select_first_message"), vimFirstShortcut},
        {QStringLiteral("select_last_message"), vimLastShortcut},
        {QStringLiteral("vim_scroll_message_down"), vimScrollDownShortcut},
        {QStringLiteral("vim_scroll_message_up"), vimScrollUpShortcut},
        {QStringLiteral("vim_tag_deleted"), vimDeleteShortcut},
        {QStringLiteral("vim_undo_tag"), vimUndoShortcut},
        {QStringLiteral("vim_tag_archived"), vimArchiveShortcut},
        {QStringLiteral("vim_tag_spam"), vimSpamShortcut},
        {QStringLiteral("vim_apply_tags"), vimApplyShortcut},
        {QStringLiteral("vim_toggle_selected"), vimToggleSelectedShortcut},
    };
    return shortcuts;
}

void appendUnique(QList<QKeySequence> &shortcuts, const QKeySequence &shortcut)
{
    if (!shortcuts.contains(shortcut)) {
        shortcuts.append(shortcut);
    }
}

QList<QKeySequence> withoutReservedShortcuts(QList<QKeySequence> shortcuts)
{
    for (const QKeySequence &reserved : reservedShortcuts()) {
        shortcuts.removeAll(reserved);
    }
    return shortcuts;
}

void updateShortcuts(QAction *action, const QList<QKeySequence> &defaults, const QList<QKeySequence> &current)
{
    if (KActionCollection::defaultShortcuts(action) != defaults) {
        KActionCollection::setDefaultShortcuts(action, defaults);
    }

    // setDefaultShortcuts also changes the active shortcuts. Restore the
    // user's current list, with only the deliberate Vim mapping changed.
    if (action->shortcuts() != current) {
        action->setShortcuts(current);
    }
}

void releaseReservedShortcuts(QAction *action, bool isJumpToFolder)
{
    const auto originalDefaults = KActionCollection::defaultShortcuts(action);
    const auto originalCurrent = action->shortcuts();
    auto defaults = withoutReservedShortcuts(originalDefaults);
    auto current = withoutReservedShortcuts(originalCurrent);

    if (isJumpToFolder) {
        if (originalDefaults.contains(vimNextShortcut)) {
            appendUnique(defaults, jumpToFolderFallback);
        }
        if (originalCurrent.contains(vimNextShortcut)) {
            appendUnique(current, jumpToFolderFallback);
        }
    }

    updateShortcuts(action, defaults, current);
}

void installTargetShortcut(QAction *action, const QKeySequence &targetShortcut)
{
    auto defaults = KActionCollection::defaultShortcuts(action);
    auto current = action->shortcuts();
    appendUnique(defaults, targetShortcut);
    appendUnique(current, targetShortcut);
    updateShortcuts(action, defaults, current);
}
}

void VimShortcutMapper::releaseAll(KActionCollection *actionCollection)
{
    if (!actionCollection) {
        return;
    }

    QAction *const jumpToFolder = actionCollection->action(QStringLiteral("jump_to_folder"));
    for (QAction *action : actionCollection->actions()) {
        if (action) {
            releaseReservedShortcuts(action, action == jumpToFolder);
        }
    }
}

bool VimShortcutMapper::apply(KActionCollection *actionCollection)
{
    if (!actionCollection) {
        return false;
    }

    QAction *const nextMessage = actionCollection->action(QStringLiteral("go_next_message"));
    QAction *const previousMessage = actionCollection->action(QStringLiteral("go_prev_message"));
    QAction *const firstMessage = actionCollection->action(QStringLiteral("select_first_message"));
    QAction *const lastMessage = actionCollection->action(QStringLiteral("select_last_message"));
    if (!nextMessage || !previousMessage || !firstMessage || !lastMessage) {
        return false;
    }

    // Release every reserved key before assigning any of them. In particular,
    // this avoids a transient Shift+S collision when KMail restores a shortcut
    // on a tag, filter, or other action before this plugin claims the key.
    releaseAll(actionCollection);

    for (QAction *action : actionCollection->actions()) {
        if (!action) {
            continue;
        }
        const QKeySequence targetShortcut = targetShortcuts().value(action->objectName());
        if (!targetShortcut.isEmpty()) {
            installTargetShortcut(action, targetShortcut);
        }
    }

    return true;
}
