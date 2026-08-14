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
#include <QSignalSpy>
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
    auto *nextUnread = collection.addAction(QStringLiteral("go_next_unread_text"));
    KActionCollection::setDefaultShortcuts(
        nextUnread,
        {QKeySequence(Qt::Key_Space), QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_Space))});

    QTRY_VERIFY(next->shortcuts().contains(QKeySequence(Qt::Key_J)));
    QVERIFY(previous->shortcuts().contains(QKeySequence(Qt::Key_K)));
    QVERIFY(first->shortcuts().contains(
        QKeySequence(QKeyCombination(Qt::NoModifier, Qt::Key_G), QKeyCombination(Qt::NoModifier, Qt::Key_G))));
    QVERIFY(last->shortcuts().contains(QKeySequence(QKeyCombination(Qt::ShiftModifier, Qt::Key_G))));
    QCOMPARE(jump->shortcuts(),
             QList<QKeySequence>({QKeySequence(QKeyCombination(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_J))}));
    QVERIFY(!replyAll->shortcuts().contains(QKeySequence(Qt::Key_A)));
    QVERIFY(!search->shortcuts().contains(QKeySequence(Qt::Key_S)));
    QCOMPARE(nextUnread->shortcuts(),
             QList<QKeySequence>({QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_Space))}));

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
    QAction *const selectedAction = collection.action(QStringLiteral("vim_toggle_selected"));
    QVERIFY(selectedAction);
    QVERIFY(selectedAction->shortcuts().contains(QKeySequence(Qt::Key_Space)));

    // KMail 26.04 does not call updateActions() for main-view generic plugins.
    // Command actions must therefore be enabled before their first activation;
    // KMail supplies the current selection in response to this signal.
    QAction *const archiveAction = collection.action(QStringLiteral("vim_tag_archived"));
    QVERIFY(archiveAction->isEnabled());
    QVERIFY(selectedAction->isEnabled());
    QSignalSpy activationSpy(interface, &PimCommon::GenericPluginInterface::emitPluginActivated);
    QSignalSpy messageSpy(interface, &PimCommon::GenericPluginInterface::message);
    connect(interface,
            &PimCommon::GenericPluginInterface::emitPluginActivated,
            interface,
            [interface](PimCommon::AbstractGenericPluginInterface *) {
                interface->setItems({});
                interface->exec();
            });
    selectedAction->trigger();
    archiveAction->trigger();
    QCOMPARE(activationSpy.count(), 2);

    int emptySelectionMessages = 0;
    for (const QList<QVariant> &arguments : messageSpy) {
        if (arguments.constFirst().toString() == QStringLiteral("Nenhuma mensagem está selecionada.")) {
            ++emptySelectionMessages;
        }
    }
    QCOMPARE(emptySelectionMessages, 2);
}

QTEST_MAIN(PluginLoadTest)

#include "pluginloadtest.moc"
