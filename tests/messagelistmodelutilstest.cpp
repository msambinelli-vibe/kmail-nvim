/*
    SPDX-FileCopyrightText: 2026 KMail Vim Navigation contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "messagelistmodelutils.h"

#include <Akonadi/EntityTreeModel>

#include <QItemSelectionModel>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QTest>
#include <QTreeView>

class MessageListModelUtilsTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void returnsEmptyForMissingViewOrModel();
    void enumeratesVisibleMessagesRecursivelyAndWithoutDuplicates();
    void enumerationDoesNotDependOnMouseSelection();
};

namespace
{
QStandardItem *messageItem(Akonadi::Item::Id id)
{
    auto *item = new QStandardItem;
    item->setData(id, Akonadi::EntityTreeModel::ItemIdRole);
    return item;
}
}

void MessageListModelUtilsTest::returnsEmptyForMissingViewOrModel()
{
    QVERIFY(MessageListModelUtils::visibleItemIds(nullptr).isEmpty());

    QTreeView view;
    QVERIFY(MessageListModelUtils::visibleItemIds(&view).isEmpty());
}

void MessageListModelUtilsTest::enumeratesVisibleMessagesRecursivelyAndWithoutDuplicates()
{
    QStandardItemModel model;
    auto *first = messageItem(11);
    model.appendRow(first);

    auto *threadHeader = new QStandardItem;
    threadHeader->appendRow(messageItem(22));
    threadHeader->appendRow(messageItem(11));
    threadHeader->appendRow(messageItem(-1));
    model.appendRow(threadHeader);

    model.appendRow(messageItem(33));

    auto *hiddenChildHeader = new QStandardItem;
    hiddenChildHeader->appendRow(messageItem(44));
    threadHeader->appendRow(hiddenChildHeader);

    QTreeView view;
    view.setModel(&model);
    view.setRowHidden(2, {}, true);
    view.setRowHidden(3, threadHeader->index(), true);

    QCOMPARE(MessageListModelUtils::visibleItemIds(&view), QList<Akonadi::Item::Id>({11, 22}));
}

void MessageListModelUtilsTest::enumerationDoesNotDependOnMouseSelection()
{
    QStandardItemModel model;
    model.appendRow(messageItem(101));
    model.appendRow(messageItem(202));
    model.appendRow(messageItem(303));

    QTreeView view;
    view.setModel(&model);
    view.selectionModel()->select(model.index(1, 0), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    view.selectionModel()->setCurrentIndex(model.index(1, 0), QItemSelectionModel::NoUpdate);

    QCOMPARE(view.selectionModel()->selectedRows().size(), 1);
    QCOMPARE(MessageListModelUtils::visibleItemIds(&view), QList<Akonadi::Item::Id>({101, 202, 303}));
}

QTEST_MAIN(MessageListModelUtilsTest)

#include "messagelistmodelutilstest.moc"
