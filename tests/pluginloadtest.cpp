/*
    SPDX-FileCopyrightText: 2026 KMail Vim Navigation contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <PimCommon/GenericPlugin>
#include <PimCommonAkonadi/GenericPluginInterface>

#include <KActionCollection>
#include <KPluginFactory>
#include <KPluginMetaData>

#include <QAction>
#include <QByteArray>
#include <QKeyCombination>
#include <QKeySequence>
#include <QObject>
#include <QTest>

class PluginLoadTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void loadsThroughKPluginFactoryAndMapsAllDeferredActions();
};

void PluginLoadTest::loadsThroughKPluginFactoryAndMapsAllDeferredActions()
{
    const QByteArray pluginPath = qgetenv("KMAIL_VIM_PLUGIN_PATH");
    QVERIFY2(!pluginPath.isEmpty(), "KMAIL_VIM_PLUGIN_PATH is not set");

    const KPluginMetaData metadata(QString::fromLocal8Bit(pluginPath));
    QVERIFY(metadata.isValid());
    QCOMPARE(metadata.version(), QStringLiteral("1.0"));
    QVERIFY(metadata.isEnabledByDefault());

    auto result = KPluginFactory::instantiatePlugin<PimCommon::GenericPlugin>(metadata, this);
    QVERIFY2(result.plugin, qPrintable(result.errorString));

    auto *interface = qobject_cast<PimCommon::GenericPluginInterface *>(result.plugin->createInterface(this));
    QVERIFY(interface);

    KActionCollection collection(this);
    interface->createAction(&collection);

    auto *next = collection.addAction(QStringLiteral("go_next_message"));
    KActionCollection::setDefaultShortcut(next, QKeySequence(Qt::Key_N));
    auto *previous = collection.addAction(QStringLiteral("go_prev_message"));
    KActionCollection::setDefaultShortcut(previous, QKeySequence(Qt::Key_P));
    auto *first = collection.addAction(QStringLiteral("select_first_message"));
    KActionCollection::setDefaultShortcut(first, QKeySequence(QKeyCombination(Qt::AltModifier, Qt::Key_Home)));
    auto *last = collection.addAction(QStringLiteral("select_last_message"));
    KActionCollection::setDefaultShortcut(last, QKeySequence(QKeyCombination(Qt::AltModifier, Qt::Key_End)));
    auto *jump = collection.addAction(QStringLiteral("jump_to_folder"));
    KActionCollection::setDefaultShortcut(jump, QKeySequence(Qt::Key_J));
    auto *replyAll = collection.addAction(QStringLiteral("reply_all"));
    KActionCollection::setDefaultShortcut(replyAll, QKeySequence(Qt::Key_A));
    auto *search = collection.addAction(QStringLiteral("search_messages"));
    KActionCollection::setDefaultShortcut(search, QKeySequence(Qt::Key_S));

    QTRY_VERIFY(next->shortcuts().contains(QKeySequence(Qt::Key_J)));
    QVERIFY(previous->shortcuts().contains(QKeySequence(Qt::Key_K)));
    QVERIFY(first->shortcuts().contains(
        QKeySequence(QKeyCombination(Qt::NoModifier, Qt::Key_G), QKeyCombination(Qt::NoModifier, Qt::Key_G))));
    QVERIFY(last->shortcuts().contains(QKeySequence(QKeyCombination(Qt::ShiftModifier, Qt::Key_G))));
    QCOMPARE(jump->shortcuts(),
             QList<QKeySequence>({QKeySequence(QKeyCombination(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_J))}));
    QVERIFY(!replyAll->shortcuts().contains(QKeySequence(Qt::Key_A)));
    QVERIFY(!search->shortcuts().contains(QKeySequence(Qt::Key_S)));

    QVERIFY(collection.action(QStringLiteral("vim_scroll_message_down"))
                ->shortcuts()
                .contains(QKeySequence(QKeyCombination(Qt::ShiftModifier, Qt::Key_J))));
    QVERIFY(collection.action(QStringLiteral("vim_scroll_message_up"))
                ->shortcuts()
                .contains(QKeySequence(QKeyCombination(Qt::ShiftModifier, Qt::Key_K))));
    QVERIFY(collection.action(QStringLiteral("vim_tag_deleted"))
                ->shortcuts()
                .contains(QKeySequence(QKeyCombination(Qt::NoModifier, Qt::Key_D), QKeyCombination(Qt::NoModifier, Qt::Key_D))));
    QVERIFY(collection.action(QStringLiteral("vim_undo_tag"))->shortcuts().contains(QKeySequence(Qt::Key_U)));
    QVERIFY(collection.action(QStringLiteral("vim_tag_archived"))->shortcuts().contains(QKeySequence(Qt::Key_A)));
    QVERIFY(collection.action(QStringLiteral("vim_tag_spam"))->shortcuts().contains(QKeySequence(Qt::Key_S)));
    QVERIFY(collection.action(QStringLiteral("vim_apply_tags"))
                ->shortcuts()
                .contains(QKeySequence(QKeyCombination(Qt::ShiftModifier, Qt::Key_S))));
}

QTEST_MAIN(PluginLoadTest)

#include "pluginloadtest.moc"
