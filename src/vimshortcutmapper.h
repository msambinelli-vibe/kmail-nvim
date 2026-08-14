/*
    SPDX-FileCopyrightText: 2026 KMail Vim Navigation contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

class KActionCollection;

namespace VimShortcutMapper
{
/**
 * Temporarily releases every shortcut reserved by the plugin.
 *
 * KMail emits KActionCollection::inserted before assigning an action's default
 * shortcut. Calling this synchronously from that signal prevents a newly
 * created tag/filter action from ever overlapping a plugin shortcut.
 */
void releaseAll(KActionCollection *actionCollection);

/**
 * Installs the Vim navigation shortcuts on KMail's native actions and reserves
 * the plugin command shortcuts.
 *
 * Existing unrelated shortcuts are preserved. Conflicting Vim shortcuts are
 * removed from other actions for the lifetime of the KMail process. KMail's
 * native "Jump to Folder" shortcut is moved from j to Ctrl+Shift+j.
 *
 * @return true when all four native list-navigation actions were found and
 * updated.
 */
[[nodiscard]] bool apply(KActionCollection *actionCollection);
}
