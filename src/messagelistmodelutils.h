/*
    SPDX-FileCopyrightText: 2026 KMail Vim Navigation contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <Akonadi/Item>

#include <QList>

class QTreeView;

namespace MessageListModelUtils
{
[[nodiscard]] QList<Akonadi::Item::Id> visibleItemIds(const QTreeView *view);
}
