/*
    SPDX-FileCopyrightText: 2026 KMail Vim Navigation contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "quickfilterdialog.h"

#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace
{
QLabel *wrappedLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setWordWrap(true);
    return label;
}

void addRadioRow(QListWidget *list, const QString &text)
{
    auto *item = new QListWidgetItem(text, list);
    item->setData(Qt::UserRole, text);
}
}

QuickFilterDialog::QuickFilterDialog(const QString &accountName,
                                     const QList<QuickFilter::Condition> &conditions,
                                     QWidget *parent)
    : QDialog(parent)
    , mConditions(conditions)
{
    setObjectName(QStringLiteral("vimQuickFilterDialog"));
    setWindowTitle(accountName.isEmpty() ? tr("Criar filtro rápido")
                                         : tr("Criar filtro rápido — %1").arg(accountName));
    setWindowModality(Qt::WindowModal);
    resize(760, 560);

    auto *layout = new QVBoxLayout(this);
    mStepLabel = new QLabel(this);
    QFont headingFont = mStepLabel->font();
    headingFont.setBold(true);
    mStepLabel->setFont(headingFont);
    layout->addWidget(mStepLabel);

    mPages = new QStackedWidget(this);
    mPages->addWidget(createConditionsPage());
    mPages->addWidget(createActionPage());
    mPages->addWidget(createApplicationPage());
    layout->addWidget(mPages, 1);

    mStatusLabel = wrappedLabel({}, this);
    mStatusLabel->setObjectName(QStringLiteral("quickFilterStatus"));
    mStatusLabel->hide();
    layout->addWidget(mStatusLabel);

    auto *buttons = new QDialogButtonBox(this);
    mBackButton = buttons->addButton(tr("Voltar"), QDialogButtonBox::ActionRole);
    mNextButton = buttons->addButton(tr("Próxima"), QDialogButtonBox::AcceptRole);
    mCancelButton = buttons->addButton(QDialogButtonBox::Cancel);
    mCancelButton->setText(tr("Cancelar"));
    connect(mBackButton, &QPushButton::clicked, this, &QuickFilterDialog::goBack);
    connect(mNextButton, &QPushButton::clicked, this, &QuickFilterDialog::goForward);
    connect(mCancelButton, &QPushButton::clicked, this, &QDialog::reject);
    layout->addWidget(buttons);

    mConditionList->installEventFilter(this);
    mActionList->installEventFilter(this);
    mApplicationList->installEventFilter(this);
    mPreviewList->installEventFilter(this);
    mEditor->installEventFilter(this);

    setPage(0);
}

QWidget *QuickFilterDialog::createConditionsPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins({});
    layout->addWidget(wrappedLabel(tr("Marque uma ou mais condições. Todas as condições marcadas deverão coincidir (AND)."), page));

    mConditionList = new QListWidget(page);
    mConditionList->setObjectName(QStringLiteral("quickFilterConditions"));
    mConditionList->setAlternatingRowColors(true);
    for (qsizetype row = 0; row < mConditions.size(); ++row) {
        const QuickFilter::Condition &condition = mConditions.at(row);
        auto *item = new QListWidgetItem(QuickFilter::conditionLabel(condition), mConditionList);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(condition.enabled ? Qt::Checked : Qt::Unchecked);
    }
    if (mConditionList->count() > 0) {
        mConditionList->setCurrentRow(0);
    }
    connect(mConditionList, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
        const int row = mConditionList->row(item);
        if (row >= 0 && row < mConditions.size()) {
            mConditions[row].enabled = item->checkState() == Qt::Checked;
            updateButtons();
            Q_EMIT previewRequested();
        }
    });
    layout->addWidget(mConditionList, 1);

    mEditor = new QLineEdit(page);
    mEditor->setObjectName(QStringLiteral("quickFilterValueEditor"));
    mEditor->setPlaceholderText(tr("Editar valor da condição"));
    mEditor->hide();
    layout->addWidget(mEditor);
    layout->addWidget(wrappedLabel(tr("j/k: navegar · Espaço: marcar · Tab: marcar e avançar · e: editar · Enter: próxima tela · Esc/q: voltar"),
                                   page));
    return page;
}

QWidget *QuickFilterDialog::createActionPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins({});
    layout->addWidget(wrappedLabel(tr("O filtro apenas adicionará uma tag. Execute S depois para efetivar a operação."), page));
    mActionList = new QListWidget(page);
    mActionList->setObjectName(QStringLiteral("quickFilterActions"));
    addRadioRow(mActionList, tr("Marcar para exclusão [deleted]"));
    addRadioRow(mActionList, tr("Marcar como spam [spam]"));
    addRadioRow(mActionList, tr("Marcar para arquivamento [archived]"));
    mActionList->setCurrentRow(0);
    connect(mActionList, &QListWidget::currentRowChanged, this, [this] {
        refreshRadioRows(mActionList);
    });
    connect(mActionList, &QListWidget::itemClicked, this, [this] {
        refreshRadioRows(mActionList);
    });
    refreshRadioRows(mActionList);
    layout->addWidget(mActionList, 1);
    layout->addWidget(wrappedLabel(tr("j/k: navegar · Espaço/Enter: escolher · Esc/q: voltar"), page));
    return page;
}

QWidget *QuickFilterDialog::createApplicationPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins({});
    layout->addWidget(wrappedLabel(tr("A regra ficará ativa para novas mensagens em todas as Inboxes."), page));
    mApplicationList = new QListWidget(page);
    mApplicationList->setObjectName(QStringLiteral("quickFilterApplication"));
    addRadioRow(mApplicationList, tr("Aplicar retroativamente às mensagens desta pasta"));
    addRadioRow(mApplicationList, tr("Aplicar também à mensagem atual"));
    addRadioRow(mApplicationList, tr("Para novas mensagens apenas"));
    mApplicationList->setCurrentRow(1);
    connect(mApplicationList, &QListWidget::currentRowChanged, this, [this] {
        refreshRadioRows(mApplicationList);
        updateButtons();
    });
    refreshRadioRows(mApplicationList);
    layout->addWidget(mApplicationList);

    mPreviewSummary = new QLabel(tr("Prévia: carregando mensagens da pasta…"), page);
    layout->addWidget(mPreviewSummary);
    mPreviewList = new QListWidget(page);
    mPreviewList->setObjectName(QStringLiteral("quickFilterPreview"));
    mPreviewList->setAlternatingRowColors(true);
    layout->addWidget(mPreviewList, 1);
    mPreviewPageLabel = new QLabel(page);
    layout->addWidget(mPreviewPageLabel);
    layout->addWidget(wrappedLabel(tr("j/k: navegar · Ctrl+d/Ctrl+u: página seguinte/anterior · Enter: criar filtro · Esc/q: voltar"),
                                   page));
    return page;
}

QList<QuickFilter::Condition> QuickFilterDialog::selectedConditions() const
{
    QList<QuickFilter::Condition> result;
    for (const QuickFilter::Condition &condition : mConditions) {
        if (condition.enabled && !condition.value.trimmed().isEmpty()) {
            result.push_back(condition);
        }
    }
    return result;
}

QuickFilter::WorkflowAction QuickFilterDialog::workflowAction() const
{
    switch (mActionList->currentRow()) {
    case 1:
        return QuickFilter::WorkflowAction::Spam;
    case 2:
        return QuickFilter::WorkflowAction::Archived;
    default:
        return QuickFilter::WorkflowAction::Deleted;
    }
}

QuickFilter::ExistingMessages QuickFilterDialog::existingMessagesMode() const
{
    switch (mApplicationList->currentRow()) {
    case 0:
        return QuickFilter::ExistingMessages::CurrentFolder;
    case 2:
        return QuickFilter::ExistingMessages::FutureOnly;
    default:
        return QuickFilter::ExistingMessages::CurrentMessage;
    }
}

void QuickFilterDialog::setPreview(const QStringList &rows, int totalMatches)
{
    mPreviewRows = rows;
    mPreviewTotal = totalMatches;
    const int pageCount = std::max(1, (static_cast<int>(mPreviewRows.size()) + previewPageSize - 1) / previewPageSize);
    mPreviewPage = std::clamp(mPreviewPage, 0, pageCount - 1);
    mPreviewSummary->setText(tr("Prévia: %1 mensagem(ns) nesta pasta").arg(totalMatches));
    mPreviewSummary->setStyleSheet({});
    refreshPreviewPage();
    updateButtons();
}

void QuickFilterDialog::setPreviewError(const QString &error)
{
    mPreviewRows.clear();
    mPreviewTotal = -1;
    mPreviewSummary->setText(tr("Não foi possível calcular a prévia: %1").arg(error));
    mPreviewSummary->setStyleSheet(QStringLiteral("color: palette(link);"));
    refreshPreviewPage();
    updateButtons();
}

void QuickFilterDialog::setBusy(bool busy, const QString &message)
{
    mBusy = busy;
    mPages->setEnabled(!busy);
    mBackButton->setEnabled(!busy && mPages->currentIndex() > 0);
    mCancelButton->setEnabled(!busy);
    mStatusLabel->setVisible(!message.isEmpty());
    mStatusLabel->setText(message);
    updateButtons();
}

void QuickFilterDialog::showError(const QString &error)
{
    setBusy(false);
    mStatusLabel->setText(error);
    mStatusLabel->setStyleSheet(QStringLiteral("color: palette(link);"));
    mStatusLabel->show();
}

void QuickFilterDialog::complete(const QString &message)
{
    mStatusLabel->setStyleSheet({});
    mStatusLabel->setText(message);
    mStatusLabel->show();
    accept();
}

bool QuickFilterDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() != QEvent::KeyPress) {
        return QDialog::eventFilter(watched, event);
    }
    auto *keyEvent = static_cast<QKeyEvent *>(event);
    if (watched == mEditor && mEditingRow >= 0 && keyEvent->key() != Qt::Key_Escape
        && keyEvent->key() != Qt::Key_Return && keyEvent->key() != Qt::Key_Enter) {
        return false;
    }
    keyEvent->setAccepted(false);
    keyPressEvent(keyEvent);
    return keyEvent->isAccepted();
}

void QuickFilterDialog::keyPressEvent(QKeyEvent *event)
{
    if (mEditingRow >= 0) {
        if (event->key() == Qt::Key_Escape) {
            finishEditingCondition(false);
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            finishEditingCondition(true);
            event->accept();
            return;
        }
        QDialog::keyPressEvent(event);
        return;
    }
    if (mBusy) {
        event->accept();
        return;
    }

    const Qt::KeyboardModifiers modifiers = event->modifiers();
    if (mPages->currentIndex() == 2 && modifiers == Qt::ControlModifier
        && (event->key() == Qt::Key_D || event->key() == Qt::Key_U)) {
        mPreviewPage += event->key() == Qt::Key_D ? 1 : -1;
        const int pageCount = std::max(1, (static_cast<int>(mPreviewRows.size()) + previewPageSize - 1) / previewPageSize);
        mPreviewPage = std::clamp(mPreviewPage, 0, pageCount - 1);
        refreshPreviewPage();
        event->accept();
        return;
    }
    if (modifiers == Qt::NoModifier && event->key() == Qt::Key_J) {
        moveCurrentRow(1);
        event->accept();
        return;
    }
    if (modifiers == Qt::NoModifier && event->key() == Qt::Key_K) {
        moveCurrentRow(-1);
        event->accept();
        return;
    }
    if (modifiers == Qt::NoModifier && (event->key() == Qt::Key_Space || event->key() == Qt::Key_Tab)) {
        activateCurrentRow(event->key() == Qt::Key_Tab);
        event->accept();
        return;
    }
    if (modifiers == Qt::NoModifier && event->key() == Qt::Key_E && mPages->currentIndex() == 0) {
        startEditingCondition();
        event->accept();
        return;
    }
    if (modifiers == Qt::NoModifier && (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Q)) {
        goBack();
        event->accept();
        return;
    }
    if (modifiers == Qt::NoModifier && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)) {
        goForward();
        event->accept();
        return;
    }
    QDialog::keyPressEvent(event);
}

void QuickFilterDialog::goBack()
{
    if (mPages->currentIndex() == 0) {
        reject();
    } else {
        setPage(mPages->currentIndex() - 1);
    }
}

void QuickFilterDialog::goForward()
{
    if (mPages->currentIndex() == 0 && selectedConditions().isEmpty()) {
        showError(tr("Marque ao menos uma condição."));
        return;
    }
    if (mPages->currentIndex() < 2) {
        setPage(mPages->currentIndex() + 1);
        return;
    }
    if (existingMessagesMode() == QuickFilter::ExistingMessages::CurrentFolder && mPreviewTotal < 0) {
        showError(tr("A aplicação retroativa requer uma prévia concluída."));
        return;
    }
    Q_EMIT finishRequested();
}

void QuickFilterDialog::setPage(int index)
{
    mPages->setCurrentIndex(std::clamp(index, 0, 2));
    static const QStringList headings = {tr("1/3 — Condições"), tr("2/3 — Ação"), tr("3/3 — Aplicação e prévia")};
    mStepLabel->setText(headings.at(mPages->currentIndex()));
    mStatusLabel->hide();
    mStatusLabel->setStyleSheet({});
    updateButtons();
    if (QListWidget *list = currentList()) {
        list->setFocus();
    }
    if (mPages->currentIndex() == 2) {
        Q_EMIT previewRequested();
    }
}

QListWidget *QuickFilterDialog::currentList() const
{
    switch (mPages->currentIndex()) {
    case 0:
        return mConditionList;
    case 1:
        return mActionList;
    default:
        return mApplicationList;
    }
}

void QuickFilterDialog::moveCurrentRow(int delta)
{
    QListWidget *const list = currentList();
    if (!list || list->count() == 0) {
        return;
    }
    const int current = std::max(0, list->currentRow());
    list->setCurrentRow(std::clamp(current + delta, 0, list->count() - 1));
}

void QuickFilterDialog::activateCurrentRow(bool advanceAfterToggle)
{
    QListWidget *const list = currentList();
    if (!list || list->currentRow() < 0) {
        return;
    }
    if (list == mConditionList) {
        QListWidgetItem *const item = list->currentItem();
        item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
        if (advanceAfterToggle) {
            moveCurrentRow(1);
        }
    } else {
        refreshRadioRows(list);
    }
}

void QuickFilterDialog::startEditingCondition()
{
    const int row = mConditionList->currentRow();
    if (row < 0 || row >= mConditions.size()) {
        return;
    }
    mEditingRow = row;
    mEditor->setText(mConditions.at(row).value);
    mEditor->show();
    mEditor->setFocus();
    mEditor->selectAll();
}

void QuickFilterDialog::finishEditingCondition(bool acceptChanges)
{
    if (mEditingRow < 0) {
        return;
    }
    const int row = mEditingRow;
    if (acceptChanges) {
        const QString value = mEditor->text().trimmed();
        if (!value.isEmpty()) {
            mConditions[row].value = value;
            refreshConditionRow(row);
            Q_EMIT previewRequested();
        }
    }
    mEditingRow = -1;
    mEditor->hide();
    mConditionList->setFocus();
    updateButtons();
}

void QuickFilterDialog::refreshConditionRow(int row)
{
    if (row >= 0 && row < mConditions.size()) {
        mConditionList->item(row)->setText(QuickFilter::conditionLabel(mConditions.at(row)));
    }
}

void QuickFilterDialog::refreshRadioRows(QListWidget *list)
{
    if (!list) {
        return;
    }
    for (int row = 0; row < list->count(); ++row) {
        QListWidgetItem *const item = list->item(row);
        const QString text = item->data(Qt::UserRole).toString();
        item->setText(QStringLiteral("%1 %2").arg(row == list->currentRow() ? QStringLiteral("(●)") : QStringLiteral("( )"), text));
    }
}

void QuickFilterDialog::refreshPreviewPage()
{
    mPreviewList->clear();
    const int pageCount = std::max(1, (static_cast<int>(mPreviewRows.size()) + previewPageSize - 1) / previewPageSize);
    const int begin = mPreviewPage * previewPageSize;
    const int end = std::min(begin + previewPageSize, static_cast<int>(mPreviewRows.size()));
    for (int index = begin; index < end; ++index) {
        mPreviewList->addItem(mPreviewRows.at(index));
    }
    if (mPreviewList->count() > 0) {
        mPreviewList->setCurrentRow(0);
    }
    mPreviewPageLabel->setText(tr("Página %1/%2 — até %3 mensagens por página")
                                   .arg(mPreviewPage + 1)
                                   .arg(pageCount)
                                   .arg(previewPageSize));
}

void QuickFilterDialog::updateButtons()
{
    const int page = mPages->currentIndex();
    mBackButton->setEnabled(!mBusy && page > 0);
    mNextButton->setText(page == 2 ? tr("Criar filtro") : tr("Próxima"));
    bool enabled = !mBusy;
    if (page == 0) {
        enabled = enabled && !selectedConditions().isEmpty();
    } else if (page == 2 && existingMessagesMode() == QuickFilter::ExistingMessages::CurrentFolder) {
        enabled = enabled && mPreviewTotal >= 0;
    }
    mNextButton->setEnabled(enabled);
}
