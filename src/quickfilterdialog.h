/*
    SPDX-FileCopyrightText: 2026 KMail Vim Navigation contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "quickfiltermodel.h"

#include <QDialog>
#include <QStringList>

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QStackedWidget;

class QuickFilterDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit QuickFilterDialog(const QString &accountName,
                               const QList<QuickFilter::Condition> &conditions,
                               QWidget *parent = nullptr);

    [[nodiscard]] QList<QuickFilter::Condition> selectedConditions() const;
    [[nodiscard]] QuickFilter::WorkflowAction workflowAction() const;
    [[nodiscard]] QuickFilter::ExistingMessages existingMessagesMode() const;

    void setPreview(const QStringList &rows, int totalMatches);
    void setPreviewError(const QString &error);
    void setBusy(bool busy, const QString &message = {});
    void showError(const QString &error);
    void complete(const QString &message);

Q_SIGNALS:
    void previewRequested();
    void finishRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    QWidget *createConditionsPage();
    QWidget *createActionPage();
    QWidget *createApplicationPage();
    void goBack();
    void goForward();
    void setPage(int index);
    void moveCurrentRow(int delta);
    void activateCurrentRow(bool advanceAfterToggle);
    void startEditingCondition();
    void finishEditingCondition(bool acceptChanges);
    void refreshConditionRow(int row);
    void refreshRadioRows(QListWidget *list);
    void refreshPreviewPage();
    void updateControls();
    void updatePageChrome();
    [[nodiscard]] QListWidget *currentList() const;

    QList<QuickFilter::Condition> mConditions;
    QStackedWidget *mPages = nullptr;
    QLabel *mAccountLabel = nullptr;
    QLabel *mStepLabel = nullptr;
    QLabel *mPageTitleLabel = nullptr;
    QLabel *mPageDescriptionLabel = nullptr;
    QLabel *mStatusLabel = nullptr;
    QLabel *mHintLabel = nullptr;
    QListWidget *mConditionList = nullptr;
    QListWidget *mActionList = nullptr;
    QListWidget *mApplicationList = nullptr;
    QLabel *mPreviewSummary = nullptr;
    QListWidget *mPreviewList = nullptr;
    QLabel *mPreviewPageLabel = nullptr;
    QLineEdit *mEditor = nullptr;
    QStringList mPreviewRows;
    int mPreviewTotal = -1;
    int mPreviewPage = 0;
    int mEditingRow = -1;
    bool mBusy = false;
    static constexpr int previewPageSize = 20;
};
