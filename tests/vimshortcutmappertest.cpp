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
    void releasesConflictsBeforeInstallingTargetShortcuts();
    void releasesTargetsBeforeLateActionGetsItsShortcut();
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
    auto *toggleSelected = addAction(collection,
                                     QStringLiteral("vim_toggle_selected"),
                                     {QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_Space))});
    auto *deleted = addAction(collection,
                              QStringLiteral("vim_tag_deleted"),
                              {QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_D)),
                               QKeySequence(QKeyCombination(Qt::NoModifier, Qt::Key_D),
                                            QKeyCombination(Qt::NoModifier, Qt::Key_D))});
    auto *clearSelected = addAction(collection,
                                    QStringLiteral("vim_clear_selected"),
                                    {QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_C))});
    auto *clearAll = addAction(collection,
                               QStringLiteral("vim_clear_all_tags"),
                               {QKeySequence(QKeyCombination(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_C))});
    auto *quickFilter = addAction(collection,
                                  QStringLiteral("vim_create_quick_filter"),
                                  {QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_F))});

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
    QCOMPARE(toggleSelected->shortcuts(),
             QList<QKeySequence>({QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_Space)), QKeySequence(Qt::Key_Space)}));
    QCOMPARE(deleted->shortcuts(),
             QList<QKeySequence>({QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_D)), QKeySequence(Qt::Key_D)}));
    QCOMPARE(clearSelected->shortcuts(),
             QList<QKeySequence>({QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_C)), QKeySequence(Qt::Key_C)}));
    QCOMPARE(clearAll->shortcuts(),
             QList<QKeySequence>({QKeySequence(QKeyCombination(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_C)),
                                  QKeySequence(QKeyCombination(Qt::ShiftModifier, Qt::Key_C))}));
    QCOMPARE(quickFilter->shortcuts(),
             QList<QKeySequence>({QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_F)),
                                  QKeySequence(QKeyCombination(Qt::NoModifier, Qt::Key_G),
                                               QKeyCombination(Qt::NoModifier, Qt::Key_F))}));

    QCOMPARE(KActionCollection::defaultShortcuts(next), QList<QKeySequence>({QKeySequence(Qt::Key_N), QKeySequence(Qt::Key_Right), QKeySequence(Qt::Key_J)}));
    QCOMPARE(KActionCollection::defaultShortcuts(previous),
             QList<QKeySequence>({QKeySequence(Qt::Key_P), QKeySequence(Qt::Key_Left), QKeySequence(Qt::Key_K)}));
    QCOMPARE(KActionCollection::defaultShortcuts(toggleSelected),
             QList<QKeySequence>({QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_Space)), QKeySequence(Qt::Key_Space)}));
    QCOMPARE(KActionCollection::defaultShortcuts(deleted), deleted->shortcuts());
    QCOMPARE(KActionCollection::defaultShortcuts(clearSelected), clearSelected->shortcuts());
    QCOMPARE(KActionCollection::defaultShortcuts(clearAll), clearAll->shortcuts());
    QCOMPARE(KActionCollection::defaultShortcuts(quickFilter), quickFilter->shortcuts());
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
                             QKeySequence(Qt::Key_D),
                             QKeySequence(QKeyCombination(Qt::NoModifier, Qt::Key_D),
                                          QKeyCombination(Qt::NoModifier, Qt::Key_D)),
                             QKeySequence(Qt::Key_S),
                             QKeySequence(Qt::Key_C),
                             QKeySequence(QKeyCombination(Qt::ShiftModifier, Qt::Key_C)),
                             QKeySequence(Qt::Key_Space),
                             QKeySequence(QKeyCombination(Qt::ShiftModifier, Qt::Key_J)),
                             QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_J)),
                             QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_D)),
                             QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_C)),
                             QKeySequence(QKeyCombination(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_C)),
                             QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_Space)),
                             QKeySequence(QKeyCombination(Qt::NoModifier, Qt::Key_G),
                                          QKeyCombination(Qt::NoModifier, Qt::Key_F)),
                             QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_G),
                                          QKeyCombination(Qt::NoModifier, Qt::Key_F))});

    QVERIFY(VimShortcutMapper::apply(&collection));

    QCOMPARE(other->shortcuts(),
             QList<QKeySequence>({QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_J)),
                                  QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_D)),
                                  QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_C)),
                                  QKeySequence(QKeyCombination(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_C)),
                                  QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_Space)),
                                  QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_G),
                                               QKeyCombination(Qt::NoModifier, Qt::Key_F))}));
    QCOMPARE(KActionCollection::defaultShortcuts(other), other->shortcuts());
}

void VimShortcutMapperTest::releasesConflictsBeforeInstallingTargetShortcuts()
{
    KActionCollection collection(this);
    addAction(collection, QStringLiteral("go_next_message"), {QKeySequence(Qt::Key_N)});
    addAction(collection, QStringLiteral("go_prev_message"), {QKeySequence(Qt::Key_P)});
    addAction(collection, QStringLiteral("select_first_message"), {});
    addAction(collection, QStringLiteral("select_last_message"), {});

    const QKeySequence shiftS(QKeyCombination(Qt::ShiftModifier, Qt::Key_S));
    auto *target = addAction(collection, QStringLiteral("vim_apply_tags"), {});
    auto *conflicting = addAction(collection, QStringLiteral("late_kmail_action"), {shiftS});
    bool transientConflict = false;
    connect(target, &QAction::changed, this, [target, conflicting, shiftS, &transientConflict] {
        if (target->shortcuts().contains(shiftS) && conflicting->shortcuts().contains(shiftS)) {
            transientConflict = true;
        }
    });

    QVERIFY(VimShortcutMapper::apply(&collection));

    QVERIFY(!transientConflict);
    QCOMPARE(target->shortcuts(), QList<QKeySequence>({shiftS}));
    QVERIFY(conflicting->shortcuts().isEmpty());
}

void VimShortcutMapperTest::releasesTargetsBeforeLateActionGetsItsShortcut()
{
    KActionCollection collection(this);
    addAction(collection, QStringLiteral("go_next_message"), {QKeySequence(Qt::Key_N)});
    addAction(collection, QStringLiteral("go_prev_message"), {QKeySequence(Qt::Key_P)});
    addAction(collection, QStringLiteral("select_first_message"), {});
    addAction(collection, QStringLiteral("select_last_message"), {});

    const QKeySequence shiftS(QKeyCombination(Qt::ShiftModifier, Qt::Key_S));
    auto *target = addAction(collection, QStringLiteral("vim_apply_tags"), {});
    QVERIFY(VimShortcutMapper::apply(&collection));
    QCOMPARE(target->shortcuts(), QList<QKeySequence>({shiftS}));

    // This is KMail's actual order: addAction() emits inserted(), the plugin
    // releases its targets in that callback, then KMail sets the new default.
    auto *lateAction = collection.addAction(QStringLiteral("late_kmail_action"));
    VimShortcutMapper::releaseAll(&collection);
    QVERIFY(target->shortcuts().isEmpty());
    KActionCollection::setDefaultShortcut(lateAction, shiftS);

    QCOMPARE(lateAction->shortcuts(), QList<QKeySequence>({shiftS}));
    QVERIFY(target->shortcuts().isEmpty());

    QVERIFY(VimShortcutMapper::apply(&collection));
    QVERIFY(lateAction->shortcuts().isEmpty());
    QCOMPARE(target->shortcuts(), QList<QKeySequence>({shiftS}));
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
    auto *toggleSelected = addAction(collection, QStringLiteral("vim_toggle_selected"), {});
    auto *deleted = addAction(collection, QStringLiteral("vim_tag_deleted"), {});
    auto *clearSelected = addAction(collection, QStringLiteral("vim_clear_selected"), {});
    auto *clearAll = addAction(collection, QStringLiteral("vim_clear_all_tags"), {});
    auto *quickFilter = addAction(collection, QStringLiteral("vim_create_quick_filter"), {});

    QVERIFY(VimShortcutMapper::apply(&collection));
    const auto nextShortcuts = next->shortcuts();
    const auto previousShortcuts = previous->shortcuts();
    const auto firstShortcuts = first->shortcuts();
    const auto lastShortcuts = last->shortcuts();
    const auto toggleSelectedShortcuts = toggleSelected->shortcuts();
    const auto deletedShortcuts = deleted->shortcuts();
    const auto clearSelectedShortcuts = clearSelected->shortcuts();
    const auto clearAllShortcuts = clearAll->shortcuts();
    const auto quickFilterShortcuts = quickFilter->shortcuts();
    QVERIFY(VimShortcutMapper::apply(&collection));

    QCOMPARE(next->shortcuts(), nextShortcuts);
    QCOMPARE(previous->shortcuts(), previousShortcuts);
    QCOMPARE(first->shortcuts(), firstShortcuts);
    QCOMPARE(last->shortcuts(), lastShortcuts);
    QCOMPARE(toggleSelected->shortcuts(), toggleSelectedShortcuts);
    QCOMPARE(deleted->shortcuts(), deletedShortcuts);
    QCOMPARE(clearSelected->shortcuts(), clearSelectedShortcuts);
    QCOMPARE(clearAll->shortcuts(), clearAllShortcuts);
    QCOMPARE(quickFilter->shortcuts(), quickFilterShortcuts);
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
