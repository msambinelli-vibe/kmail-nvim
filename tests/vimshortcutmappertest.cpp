/*
    SPDX-FileCopyrightText: 2026 KMail Vim Navigation contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "vimshortcutmapper.h"

#include <KActionCollection>

#include <QAction>
#include <QKeyCombination>
#include <QKeySequence>
#include <QObject>
#include <QTest>

class VimShortcutMapperTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void mapsAllNavigationAndPreservesExistingShortcuts();
    void reservesPluginKeysWithoutRemovingModifiedKeys();
    void preservesCustomJumpToFolderShortcut();
    void isIdempotent();
    void failsCleanlyWhenNavigationActionsAreMissing();
};

namespace
{
QAction *addAction(KActionCollection &collection, const QString &name, const QList<QKeySequence> &defaults)
{
    auto *action = collection.addAction(name);
    KActionCollection::setDefaultShortcuts(action, defaults);
    return action;
}
}

void VimShortcutMapperTest::mapsAllNavigationAndPreservesExistingShortcuts()
{
    KActionCollection collection(this);
    auto *next = addAction(collection, QStringLiteral("go_next_message"), {QKeySequence(Qt::Key_N), QKeySequence(Qt::Key_Right)});
    auto *previous = addAction(collection, QStringLiteral("go_prev_message"), {QKeySequence(Qt::Key_P), QKeySequence(Qt::Key_Left)});
    auto *first = addAction(collection, QStringLiteral("select_first_message"), {QKeySequence(QKeyCombination(Qt::AltModifier, Qt::Key_Home))});
    auto *last = addAction(collection, QStringLiteral("select_last_message"), {QKeySequence(QKeyCombination(Qt::AltModifier, Qt::Key_End))});
    auto *jump = addAction(collection, QStringLiteral("jump_to_folder"), {QKeySequence(Qt::Key_J)});

    next->setShortcuts({QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_N))});
    previous->setShortcuts({QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_P))});

    QVERIFY(VimShortcutMapper::apply(&collection));

    QCOMPARE(next->shortcuts(), QList<QKeySequence>({QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_N)), QKeySequence(Qt::Key_J)}));
    QCOMPARE(previous->shortcuts(), QList<QKeySequence>({QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_P)), QKeySequence(Qt::Key_K)}));
    QCOMPARE(first->shortcuts(),
             QList<QKeySequence>({QKeySequence(QKeyCombination(Qt::AltModifier, Qt::Key_Home)),
                                  QKeySequence(QKeyCombination(Qt::NoModifier, Qt::Key_G), QKeyCombination(Qt::NoModifier, Qt::Key_G))}));
    QCOMPARE(last->shortcuts(),
             QList<QKeySequence>({QKeySequence(QKeyCombination(Qt::AltModifier, Qt::Key_End)),
                                  QKeySequence(QKeyCombination(Qt::ShiftModifier, Qt::Key_G))}));
    QCOMPARE(jump->shortcuts(),
             QList<QKeySequence>({QKeySequence(QKeyCombination(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_J))}));

    QCOMPARE(KActionCollection::defaultShortcuts(next), QList<QKeySequence>({QKeySequence(Qt::Key_N), QKeySequence(Qt::Key_Right), QKeySequence(Qt::Key_J)}));
    QCOMPARE(KActionCollection::defaultShortcuts(previous),
             QList<QKeySequence>({QKeySequence(Qt::Key_P), QKeySequence(Qt::Key_Left), QKeySequence(Qt::Key_K)}));
}

void VimShortcutMapperTest::reservesPluginKeysWithoutRemovingModifiedKeys()
{
    KActionCollection collection(this);
    addAction(collection, QStringLiteral("go_next_message"), {QKeySequence(Qt::Key_N)});
    addAction(collection, QStringLiteral("go_prev_message"), {QKeySequence(Qt::Key_P)});
    addAction(collection, QStringLiteral("select_first_message"), {});
    addAction(collection, QStringLiteral("select_last_message"), {});
    auto *other = addAction(collection,
                            QStringLiteral("other_action"),
                            {QKeySequence(Qt::Key_K),
                             QKeySequence(Qt::Key_A),
                             QKeySequence(Qt::Key_S),
                             QKeySequence(QKeyCombination(Qt::ShiftModifier, Qt::Key_J)),
                             QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_J))});

    QVERIFY(VimShortcutMapper::apply(&collection));

    QCOMPARE(other->shortcuts(), QList<QKeySequence>({QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_J))}));
}

void VimShortcutMapperTest::preservesCustomJumpToFolderShortcut()
{
    KActionCollection collection(this);
    addAction(collection, QStringLiteral("go_next_message"), {QKeySequence(Qt::Key_N)});
    addAction(collection, QStringLiteral("go_prev_message"), {QKeySequence(Qt::Key_P)});
    addAction(collection, QStringLiteral("select_first_message"), {});
    addAction(collection, QStringLiteral("select_last_message"), {});
    auto *jump = addAction(collection, QStringLiteral("jump_to_folder"), {QKeySequence(Qt::Key_J)});
    jump->setShortcuts({QKeySequence(QKeyCombination(Qt::AltModifier, Qt::Key_G))});

    QVERIFY(VimShortcutMapper::apply(&collection));

    QCOMPARE(jump->shortcuts(), QList<QKeySequence>({QKeySequence(QKeyCombination(Qt::AltModifier, Qt::Key_G))}));
    QCOMPARE(KActionCollection::defaultShortcuts(jump),
             QList<QKeySequence>({QKeySequence(QKeyCombination(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_J))}));
}

void VimShortcutMapperTest::isIdempotent()
{
    KActionCollection collection(this);
    auto *next = addAction(collection, QStringLiteral("go_next_message"), {QKeySequence(Qt::Key_N)});
    auto *previous = addAction(collection, QStringLiteral("go_prev_message"), {QKeySequence(Qt::Key_P)});
    auto *first = addAction(collection, QStringLiteral("select_first_message"), {});
    auto *last = addAction(collection, QStringLiteral("select_last_message"), {});

    QVERIFY(VimShortcutMapper::apply(&collection));
    const auto nextShortcuts = next->shortcuts();
    const auto previousShortcuts = previous->shortcuts();
    const auto firstShortcuts = first->shortcuts();
    const auto lastShortcuts = last->shortcuts();
    QVERIFY(VimShortcutMapper::apply(&collection));

    QCOMPARE(next->shortcuts(), nextShortcuts);
    QCOMPARE(previous->shortcuts(), previousShortcuts);
    QCOMPARE(first->shortcuts(), firstShortcuts);
    QCOMPARE(last->shortcuts(), lastShortcuts);
}

void VimShortcutMapperTest::failsCleanlyWhenNavigationActionsAreMissing()
{
    KActionCollection collection(this);
    auto *jump = addAction(collection, QStringLiteral("jump_to_folder"), {QKeySequence(Qt::Key_J)});

    QVERIFY(!VimShortcutMapper::apply(&collection));
    QCOMPARE(jump->shortcuts(), QList<QKeySequence>({QKeySequence(Qt::Key_J)}));
}

QTEST_MAIN(VimShortcutMapperTest)

#include "vimshortcutmappertest.moc"
