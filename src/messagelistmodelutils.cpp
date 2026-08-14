/*
    SPDX-FileCopyrightText: 2026 KMail Vim Navigation contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "messagelistmodelutils.h"

#include <Akonadi/EntityTreeModel>

#include <QAbstractItemModel>
#include <QModelIndex>
#include <QSet>
#include <QTreeView>

QList<Akonadi::Item::Id> MessageListModelUtils::visibleItemIds(const QTreeView *view)
{
    QList<Akonadi::Item::Id> result;
    if (!view || !view->model()) {
        return result;
    }

    const QAbstractItemModel *const model = view->model();
    QSet<Akonadi::Item::Id> seen;
    const auto walk = [&](const auto &self, const QModelIndex &parent) -> void {
        const int rowCount = model->rowCount(parent);
        for (int row = 0; row < rowCount; ++row) {
            // Quick filters in MessageList hide rows in the view instead of
            // removing them from the underlying tree model.
            if (view->isRowHidden(row, parent)) {
                continue;
            }

            const QModelIndex index = model->index(row, 0, parent);
            if (!index.isValid()) {
                continue;
            }

            bool converted = false;
            const Akonadi::Item::Id id = index.data(Akonadi::EntityTreeModel::ItemIdRole).toLongLong(&converted);
            if (converted && id > 0 && !seen.contains(id)) {
                seen.insert(id);
                result.push_back(id);
            }

            // Threads and grouping headers are represented as tree nodes.
            // Recurse even when a thread is collapsed: its messages still
            // belong to the current list.
            self(self, index);
        }
    };
    walk(walk, {});
    return result;
}
