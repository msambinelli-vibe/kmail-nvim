/*
    SPDX-FileCopyrightText: 2026 KMail Vim Navigation contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "quickfilterdialog.h"
#include "quickfiltermodel.h"

#include <KMime/Message>

#include <QDialogButtonBox>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSignalSpy>
#include <QTest>

class QuickFilterTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void extractsStrongCandidatesAndMatchesWithAnd();
    void fallsBackToMailingListHeaderRecognizedByKMail();
    void domainMatchDoesNotAcceptLongerDomain();
    void keyboardFlowSupportsToggleEditAndNavigation();
};

namespace
{
Akonadi::Item messageItem(Akonadi::Item::Id id,
                          const QByteArray &from,
                          const QByteArray &subject,
                          const QByteArray &listId = {})
{
    auto message = std::make_shared<KMime::Message>();
    QByteArray content = "From: " + from + "\nSubject: " + subject + "\n";
    if (!listId.isEmpty()) {
        content += "List-ID: Example newsletter <" + listId + ">\n";
    }
    content += "Date: Tue, 18 Aug 2026 10:00:00 +0200\n\nBody";
    message->setContent(content);
    message->parse();

    Akonadi::Item item(id);
    item.setMimeType(QStringLiteral("message/rfc822"));
    item.setPayload(message);
    return item;
}
}

void QuickFilterTest::extractsStrongCandidatesAndMatchesWithAnd()
{
    const Akonadi::Item item = messageItem(1,
                                           QByteArrayLiteral("Example Offers <offers@example.com>"),
                                           QByteArrayLiteral("Weekly offers"),
                                           QByteArrayLiteral("newsletter.example.com"));
    const auto message = item.payload<std::shared_ptr<KMime::Message>>();
    const auto conditions = QuickFilter::conditionsFromMessage(message);

    QCOMPARE(conditions.size(), 4);
    QCOMPARE(conditions.at(0).kind, QuickFilter::ConditionKind::MailingList);
    QCOMPARE(conditions.at(0).value, QStringLiteral("<newsletter.example.com>"));
    QVERIFY(conditions.at(0).enabled);
    QVERIFY(!conditions.at(1).enabled);
    QCOMPARE(conditions.at(1).value, QStringLiteral("offers@example.com"));
    QCOMPARE(conditions.at(2).value, QStringLiteral("example.com"));
    QCOMPARE(conditions.at(3).value, QStringLiteral("Weekly offers"));

    auto selected = conditions;
    selected[1].enabled = true;
    QVERIFY(QuickFilter::matches(selected, item));

    selected[3].enabled = true;
    selected[3].value = QStringLiteral("unrelated subject");
    QVERIFY(!QuickFilter::matches(selected, item));
}

void QuickFilterTest::domainMatchDoesNotAcceptLongerDomain()
{
    QuickFilter::Condition domain{QuickFilter::ConditionKind::SenderDomain,
                                  QByteArrayLiteral("From"),
                                  MailCommon::SearchRule::FuncRegExp,
                                  QStringLiteral("example.com"),
                                  true};
    QVERIFY(QuickFilter::matches({domain}, messageItem(1, QByteArrayLiteral("a@example.com"), QByteArrayLiteral("A"))));
    QVERIFY(!QuickFilter::matches({domain},
                                  messageItem(2, QByteArrayLiteral("a@example.com.evil"), QByteArrayLiteral("B"))));
}

void QuickFilterTest::fallsBackToMailingListHeaderRecognizedByKMail()
{
    auto message = std::make_shared<KMime::Message>();
    message->setContent("From: list-owner@example.com\n"
                        "Subject: List update\n"
                        "List-Post: <mailto:community@example.com>\n"
                        "Date: Tue, 18 Aug 2026 10:00:00 +0200\n\nBody");
    message->parse();

    const auto conditions = QuickFilter::conditionsFromMessage(message);
    QCOMPARE(conditions.size(), 4);
    QCOMPARE(conditions.constFirst().kind, QuickFilter::ConditionKind::MailingList);
    QCOMPARE(conditions.constFirst().field, QByteArrayLiteral("List-Post"));
    QCOMPARE(conditions.constFirst().value, QStringLiteral("<mailto:community@example.com>"));
    QVERIFY(conditions.constFirst().enabled);
    QVERIFY(!conditions.at(1).enabled);
    QCOMPARE(QuickFilter::conditionLabel(conditions.constFirst()),
             QStringLiteral("List-Post contém <mailto:community@example.com>"));

    Akonadi::Item item(3);
    item.setMimeType(QStringLiteral("message/rfc822"));
    item.setPayload(message);
    QVERIFY(QuickFilter::matches({conditions.constFirst()}, item));
}

void QuickFilterTest::keyboardFlowSupportsToggleEditAndNavigation()
{
    QList<QuickFilter::Condition> conditions = {
        {QuickFilter::ConditionKind::MailingList,
         QByteArrayLiteral("List-Id"),
         MailCommon::SearchRule::FuncContains,
         QStringLiteral("<list.example>"),
         true},
        {QuickFilter::ConditionKind::Subject,
         QByteArrayLiteral("Subject"),
         MailCommon::SearchRule::FuncContains,
         QStringLiteral("Old subject"),
         false},
    };
    QuickFilterDialog dialog(QStringLiteral("Personal"), conditions);
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));

    auto *conditionList = dialog.findChild<QListWidget *>(QStringLiteral("quickFilterConditions"));
    auto *editor = dialog.findChild<QLineEdit *>(QStringLiteral("quickFilterValueEditor"));
    auto *actionList = dialog.findChild<QListWidget *>(QStringLiteral("quickFilterActions"));
    auto *applicationList = dialog.findChild<QListWidget *>(QStringLiteral("quickFilterApplication"));
    auto *surface = dialog.findChild<QFrame *>(QStringLiteral("quickFilterSurface"));
    auto *hint = dialog.findChild<QLabel *>(QStringLiteral("quickFilterHint"));
    QVERIFY(conditionList);
    QVERIFY(editor);
    QVERIFY(actionList);
    QVERIFY(applicationList);
    QVERIFY(surface);
    QVERIFY(hint);
    QVERIFY(dialog.findChild<QDialogButtonBox *>() == nullptr);
    QVERIFY(!conditionList->alternatingRowColors());
    QVERIFY(conditionList->visualItemRect(conditionList->item(0)).height() >= 60);
    QVERIFY(hint->text().contains(QStringLiteral("j/k")));
    QCOMPARE(conditionList->currentRow(), 0);
    QTest::keyClick(conditionList, Qt::Key_Tab);
    QCOMPARE(conditionList->item(0)->checkState(), Qt::Unchecked);
    QCOMPARE(conditionList->currentRow(), 1);
    QTest::keyClick(conditionList, Qt::Key_Space);
    QCOMPARE(conditionList->item(1)->checkState(), Qt::Checked);
    QTest::keyClick(conditionList, Qt::Key_E);
    QVERIFY(editor->isVisible());
    editor->selectAll();
    QTest::keyClicks(editor, QStringLiteral("Edited subject"));
    QTest::keyClick(editor, Qt::Key_Return);
    QVERIFY(!editor->isVisible());
    QCOMPARE(dialog.selectedConditions().constFirst().value, QStringLiteral("Edited subject"));

    QTest::keyClick(conditionList, Qt::Key_Return);
    QVERIFY(actionList->isVisible());
    QTest::keyClick(actionList, Qt::Key_J);
    QCOMPARE(dialog.workflowAction(), QuickFilter::WorkflowAction::Spam);
    QTest::keyClick(actionList, Qt::Key_Return);
    QVERIFY(applicationList->isVisible());
    QCOMPARE(dialog.existingMessagesMode(), QuickFilter::ExistingMessages::CurrentMessage);

    dialog.setPreview({QStringLiteral("2026-08-18 · sender · subject")}, 1);
    QSignalSpy finishSpy(&dialog, &QuickFilterDialog::finishRequested);
    QTest::keyClick(applicationList, Qt::Key_Return);
    QCOMPARE(finishSpy.count(), 1);
}

QTEST_MAIN(QuickFilterTest)

#include "quickfiltertest.moc"
