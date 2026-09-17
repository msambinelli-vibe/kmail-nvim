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
    void recipientConditionUsesOnlyToAndSupportsEditing();
    void keyboardFlowSupportsSingleChoiceEditAndNavigation();
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

void QuickFilterTest::recipientConditionUsesOnlyToAndSupportsEditing()
{
    auto message = std::make_shared<KMime::Message>();
    message->setContent("From: sender@example.com\n"
                        "To: Alice <Alice@example.com>,\n"
                        " Bob <bob@example.com>, Alice <alice@example.com>\n"
                        "Cc: copy@example.com\n"
                        "Subject: Update\n\nBody");
    message->parse();
    const auto conditions = QuickFilter::conditionsFromMessage(message);
    QList<QuickFilter::Condition> recipients;
    int recipientRow = -1;
    for (int row = 0; row < conditions.size(); ++row) {
        if (conditions.at(row).kind == QuickFilter::ConditionKind::Recipient) {
            if (recipientRow < 0) {
                recipientRow = row;
            }
            recipients.push_back(conditions.at(row));
        }
    }
    QCOMPARE(recipients.size(), 2);
    QCOMPARE(recipients.at(0).value, QStringLiteral("alice@example.com"));
    QCOMPARE(recipients.at(1).value, QStringLiteral("bob@example.com"));
    QCOMPARE(recipients.constFirst().field, QByteArrayLiteral("To"));
    QCOMPARE(QuickFilter::conditionLabel(recipients.constFirst()), QStringLiteral("To contém alice@example.com"));

    Akonadi::Item item(4);
    item.setMimeType(QStringLiteral("message/rfc822"));
    item.setPayload(message);
    for (auto condition : recipients) {
        condition.enabled = true;
        QVERIFY(QuickFilter::matches({condition}, item));
        auto other = messageItem(5, condition.value.toUtf8(), "Other message");
        other.payload<std::shared_ptr<KMime::Message>>()->setContent(
            "From: " + condition.value.toUtf8() + "\nTo: other@example.com\nCc: "
            + condition.value.toUtf8() + "\n\nBody");
        other.payload<std::shared_ptr<KMime::Message>>()->parse();
        QVERIFY(!QuickFilter::matches({condition}, other));
    }
    const auto noToConditions = QuickFilter::conditionsFromMessage(
        messageItem(6, "sender@example.com", "No To header").payload<std::shared_ptr<KMime::Message>>());
    for (const auto &condition : noToConditions) {
        QVERIFY(condition.kind != QuickFilter::ConditionKind::Recipient);
    }

    QuickFilterDialog dialog(QStringLiteral("Personal"), conditions);
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));
    auto *list = dialog.findChild<QListWidget *>(QStringLiteral("quickFilterConditions"));
    auto *editor = dialog.findChild<QLineEdit *>(QStringLiteral("quickFilterValueEditor"));
    QVERIFY(list);
    QVERIFY(editor);
    for (int row = 0; row < recipientRow; ++row) {
        QTest::keyClick(list, Qt::Key_J);
    }
    QCOMPARE(dialog.selectedConditions().size(), 1);
    QCOMPARE(dialog.selectedConditions().constFirst().field, QByteArrayLiteral("To"));
    QTest::keyClick(list, Qt::Key_E);
    QTest::keyClicks(editor, QStringLiteral("bob@example.com"));
    QTest::keyClick(editor, Qt::Key_Return);
    const auto selected = dialog.selectedConditions();
    QCOMPARE(selected.constFirst().field, QByteArrayLiteral("To"));
    QCOMPARE(selected.constFirst().value, QStringLiteral("bob@example.com"));
    QVERIFY(QuickFilter::matches(selected, item));
    QTest::keyClick(list, Qt::Key_Return);
    QVERIFY(dialog.findChild<QListWidget *>(QStringLiteral("quickFilterActions"))->isVisible());
}

void QuickFilterTest::keyboardFlowSupportsSingleChoiceEditAndNavigation()
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
    QVERIFY(dialog.windowFlags().testFlag(Qt::FramelessWindowHint));
    for (const auto *list : {conditionList, actionList, applicationList}) {
        for (int row = 0; row < list->count(); ++row) {
            QVERIFY(!list->item(row)->flags().testFlag(Qt::ItemIsUserCheckable));
            QVERIFY(!list->item(row)->data(Qt::CheckStateRole).isValid());
        }
    }
    QVERIFY(dialog.findChild<QDialogButtonBox *>() == nullptr);
    QVERIFY(!conditionList->alternatingRowColors());
    QVERIFY(conditionList->visualItemRect(conditionList->item(0)).height() >= 60);
    QVERIFY(hint->text().contains(QStringLiteral("j/k")));
    QCOMPARE(conditionList->currentRow(), 0);
    QCOMPARE(dialog.selectedConditions().size(), 1);
    QSignalSpy previewSpy(&dialog, &QuickFilterDialog::previewRequested);
    QTest::keyClick(conditionList, Qt::Key_J);
    QCOMPARE(conditionList->currentRow(), 1);
    QCOMPARE(dialog.selectedConditions().size(), 1);
    QCOMPARE(dialog.selectedConditions().constFirst().kind, QuickFilter::ConditionKind::Subject);
    QVERIFY(dialog.selectedConditions().constFirst().enabled);
    QCOMPARE(previewSpy.count(), 1);
    QTest::keyClick(conditionList, Qt::Key_Space);
    QCOMPARE(dialog.selectedConditions().size(), 1);
    QTest::keyClick(conditionList, Qt::Key_E);
    QVERIFY(editor->isVisible());
    editor->selectAll();
    QTest::keyClicks(editor, QStringLiteral("Edited subject"));
    QTest::keyClick(editor, Qt::Key_Return);
    QVERIFY(!editor->isVisible());
    QCOMPARE(dialog.selectedConditions().constFirst().value, QStringLiteral("Edited subject"));
    QVERIFY(conditionList->isVisible());

    QTest::keyClick(conditionList, Qt::Key_Return);
    QVERIFY(actionList->isVisible());
    QTest::keyClick(actionList, Qt::Key_J);
    QCOMPARE(dialog.workflowAction(), QuickFilter::WorkflowAction::Spam);
    QTest::keyClick(actionList, Qt::Key_Return);
    QVERIFY(applicationList->isVisible());
    QCOMPARE(dialog.existingMessagesMode(), QuickFilter::ExistingMessages::CurrentFolder);

    QSignalSpy finishSpy(&dialog, &QuickFilterDialog::finishRequested);
    QTest::keyClick(applicationList, Qt::Key_Return);
    QCOMPARE(finishSpy.count(), 0); // Wait for the retroactive preview before saving.
    dialog.setPreview({QStringLiteral("2026-08-18 · sender · subject")}, 1);
    QTest::keyClick(applicationList, Qt::Key_Return);
    QCOMPARE(finishSpy.count(), 1);

    QTest::keyClick(applicationList, Qt::Key_Escape);
    QVERIFY(actionList->isVisible());
    QTest::keyClick(actionList, Qt::Key_Escape);
    QVERIFY(conditionList->isVisible());
    QCOMPARE(conditionList->currentRow(), 1);
    QTest::keyClick(conditionList, Qt::Key_K);
    QCOMPARE(dialog.selectedConditions().size(), 1);
    QCOMPARE(dialog.selectedConditions().constFirst().kind, QuickFilter::ConditionKind::MailingList);
    QTest::keyClick(conditionList, Qt::Key_J);
    QTest::keyClick(conditionList, Qt::Key_E);
    QTest::keyClicks(editor, QStringLiteral("Discard this edit"));
    QTest::keyClick(editor, Qt::Key_Escape);
    QCOMPARE(dialog.selectedConditions().constFirst().value, QStringLiteral("Edited subject"));
    QSignalSpy rejectedSpy(&dialog, &QDialog::rejected);
    QTest::keyClick(conditionList, Qt::Key_Q);
    QCOMPARE(rejectedSpy.count(), 1);
}

QTEST_MAIN(QuickFilterTest)

#include "quickfiltertest.moc"
