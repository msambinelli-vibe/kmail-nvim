/*
    SPDX-FileCopyrightText: 2026 KMail Vim Navigation contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "quickfilterdialog.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include <algorithm>

namespace
{
enum ItemDataRole {
    PrimaryTextRole = Qt::UserRole + 1,
    SecondaryTextRole,
    ChoiceMarkerRole,
};

QColor translucent(QColor color, int alpha)
{
    color.setAlpha(alpha);
    return color;
}

class ModernListDelegate final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        Q_UNUSED(option)
        Q_UNUSED(index)
        return QSize(320, 68);
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);

        const QPalette palette = option.palette;
        const QColor accent = palette.color(QPalette::Highlight);
        const bool selected = option.state.testFlag(QStyle::State_Selected);
        const bool hovered = option.state.testFlag(QStyle::State_MouseOver);
        const QRectF card = QRectF(option.rect).adjusted(4, 3, -4, -3);
        if (selected || hovered) {
            painter->setPen(selected ? QPen(translucent(accent, 110), 1.0) : Qt::NoPen);
            painter->setBrush(selected ? translucent(accent, 34) : palette.color(QPalette::AlternateBase));
            painter->drawRoundedRect(card, 11, 11);
        }

        const QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        qreal textLeft = card.left() + 14;
        if (!icon.isNull()) {
            const QRectF tile(card.left() + 12, card.center().y() - 20, 40, 40);
            painter->setPen(Qt::NoPen);
            painter->setBrush(selected ? translucent(accent, 38) : translucent(palette.color(QPalette::Text), 14));
            painter->drawRoundedRect(tile, 10, 10);
            icon.paint(painter,
                       tile.adjusted(8, 8, -8, -8).toRect(),
                       Qt::AlignCenter,
                       option.state.testFlag(QStyle::State_Enabled) ? QIcon::Normal : QIcon::Disabled);
            textLeft = tile.right() + 13;
        }

        const bool checkable = index.data(Qt::CheckStateRole).isValid();
        const bool checked = index.data(Qt::CheckStateRole).toInt() == Qt::Checked;
        const bool choice = index.data(ChoiceMarkerRole).toBool();
        const qreal markerWidth = checkable ? 40 : 12;
        const QRectF textRect(textLeft, card.top() + 10, card.right() - textLeft - markerWidth, card.height() - 20);

        QString primary = index.data(PrimaryTextRole).toString();
        if (primary.isEmpty()) {
            primary = index.data(Qt::DisplayRole).toString();
        }
        const QString secondary = index.data(SecondaryTextRole).toString();
        QFont primaryFont = option.font;
        primaryFont.setWeight(QFont::DemiBold);
        painter->setFont(primaryFont);
        painter->setPen(palette.color(QPalette::Text));
        const QFontMetrics primaryMetrics(primaryFont);
        const int primaryHeight = primaryMetrics.height();
        painter->drawText(QRectF(textRect.left(), textRect.top(), textRect.width(), primaryHeight),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          primaryMetrics.elidedText(primary, Qt::ElideRight, qRound(textRect.width())));

        if (!secondary.isEmpty()) {
            QFont secondaryFont = option.font;
            secondaryFont.setPointSizeF(std::max(8.0, secondaryFont.pointSizeF() - 1.0));
            painter->setFont(secondaryFont);
            painter->setPen(palette.color(QPalette::PlaceholderText));
            const QFontMetrics secondaryMetrics(secondaryFont);
            painter->drawText(QRectF(textRect.left(), textRect.bottom() - secondaryMetrics.height(), textRect.width(), secondaryMetrics.height()),
                              Qt::AlignLeft | Qt::AlignVCenter,
                              secondaryMetrics.elidedText(secondary, Qt::ElideRight, qRound(textRect.width())));
        }

        if (checkable) {
            const QPointF center(card.right() - 20, card.center().y());
            painter->setPen(QPen(checked ? accent : translucent(palette.color(QPalette::Text), 72), checked ? 1.5 : 1.2));
            painter->setBrush(checked ? accent : translucent(palette.color(QPalette::Text), 10));
            if (choice) {
                painter->drawEllipse(center, 9, 9);
                if (checked) {
                    painter->setBrush(palette.color(QPalette::HighlightedText));
                    painter->setPen(Qt::NoPen);
                    painter->drawEllipse(center, 3, 3);
                }
            } else {
                const QRectF box(center.x() - 9, center.y() - 9, 18, 18);
                painter->drawRoundedRect(box, 5, 5);
                if (checked) {
                    QPainterPath check;
                    check.moveTo(center.x() - 4.5, center.y());
                    check.lineTo(center.x() - 1, center.y() + 3.5);
                    check.lineTo(center.x() + 5, center.y() - 4);
                    painter->setBrush(Qt::NoBrush);
                    painter->setPen(QPen(palette.color(QPalette::HighlightedText), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                    painter->drawPath(check);
                }
            }
        }

        painter->restore();
    }
};

QLabel *label(const QString &text, const QString &objectName, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setObjectName(objectName);
    return label;
}

void prepareList(QListWidget *list)
{
    list->setAlternatingRowColors(false);
    list->setFrameShape(QFrame::NoFrame);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setMouseTracking(true);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setSpacing(1);
    list->setUniformItemSizes(true);
    list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list->setItemDelegate(new ModernListDelegate(list));
    list->setStyleSheet(QStringLiteral("QListWidget { background: transparent; border: 0; outline: 0; }"
                                       "QListWidget::item { background: transparent; border: 0; }"));
}

QString conditionTitle(QuickFilter::ConditionKind kind)
{
    switch (kind) {
    case QuickFilter::ConditionKind::ListId:
        return QObject::tr("Lista de distribuição");
    case QuickFilter::ConditionKind::Sender:
        return QObject::tr("Remetente");
    case QuickFilter::ConditionKind::SenderDomain:
        return QObject::tr("Domínio do remetente");
    case QuickFilter::ConditionKind::Subject:
        return QObject::tr("Assunto");
    }
    return {};
}

QString conditionIcon(QuickFilter::ConditionKind kind)
{
    switch (kind) {
    case QuickFilter::ConditionKind::ListId:
        return QStringLiteral("mail-message-new-list");
    case QuickFilter::ConditionKind::Sender:
        return QStringLiteral("user-identity");
    case QuickFilter::ConditionKind::SenderDomain:
        return QStringLiteral("internet-services");
    case QuickFilter::ConditionKind::Subject:
        return QStringLiteral("mail-message-new");
    }
    return {};
}

void setConditionPresentation(QListWidgetItem *item, const QuickFilter::Condition &condition)
{
    const QString primary = conditionTitle(condition.kind);
    item->setText(QuickFilter::conditionLabel(condition));
    item->setData(PrimaryTextRole, primary);
    item->setData(SecondaryTextRole, QuickFilter::conditionLabel(condition));
    item->setIcon(QIcon::fromTheme(conditionIcon(condition.kind)));
}

void addChoiceRow(QListWidget *list,
                  const QString &title,
                  const QString &description,
                  const QString &iconName)
{
    auto *item = new QListWidgetItem(title, list);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
    item->setCheckState(Qt::Unchecked);
    item->setData(PrimaryTextRole, title);
    item->setData(SecondaryTextRole, description);
    item->setData(ChoiceMarkerRole, true);
    item->setIcon(QIcon::fromTheme(iconName));
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
    resize(720, 620);
    setMinimumSize(620, 520);

    const QPalette colors = palette();
    const QColor accent = colors.color(QPalette::Highlight);
    setStyleSheet(QStringLiteral(
                      "QDialog#vimQuickFilterDialog { background: %1; }"
                      "QFrame#quickFilterSurface { background: %2; border: 1px solid %3; border-radius: 16px; }"
                      "QLabel#quickFilterTitle { color: %4; font-size: 17px; font-weight: 600; }"
                      "QLabel#quickFilterAccount, QLabel#quickFilterDescription, QLabel#quickFilterHint, QLabel#quickFilterPreviewPage { color: %5; }"
                      "QLabel#quickFilterPageTitle { color: %4; font-size: 14px; font-weight: 600; }"
                      "QLabel#quickFilterStep { color: %6; background: %7; border: 1px solid %8; border-radius: 10px; padding: 3px 9px; font-weight: 600; }"
                      "QLineEdit#quickFilterValueEditor { background: %1; color: %4; border: 1px solid %3; border-radius: 10px; padding: 10px 12px; selection-background-color: %6; }"
                      "QLineEdit#quickFilterValueEditor:focus { border-color: %6; }"
                      "QLabel#quickFilterStatus { background: %1; border-radius: 8px; padding: 8px 10px; }"
                      "QScrollBar:vertical { background: transparent; width: 7px; margin: 2px 0; }"
                      "QScrollBar::handle:vertical { background: %9; min-height: 30px; border-radius: 3px; }"
                      "QScrollBar::handle:vertical:hover { background: %10; }"
                      "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
                      "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }")
                      .arg(colors.color(QPalette::Window).name(QColor::HexArgb),
                           colors.color(QPalette::Base).name(QColor::HexArgb),
                           colors.color(QPalette::Midlight).name(QColor::HexArgb),
                           colors.color(QPalette::Text).name(QColor::HexArgb),
                           colors.color(QPalette::PlaceholderText).name(QColor::HexArgb),
                           accent.name(QColor::HexArgb),
                           translucent(accent, 28).name(QColor::HexArgb),
                           translucent(accent, 80).name(QColor::HexArgb),
                           translucent(colors.color(QPalette::Text), 48).name(QColor::HexArgb),
                           translucent(colors.color(QPalette::Text), 88).name(QColor::HexArgb)));

    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(14, 14, 14, 14);
    auto *surface = new QFrame(this);
    surface->setObjectName(QStringLiteral("quickFilterSurface"));
    outerLayout->addWidget(surface);
    auto *layout = new QVBoxLayout(surface);
    layout->setContentsMargins(22, 20, 22, 16);
    layout->setSpacing(12);

    auto *header = new QHBoxLayout;
    auto *identity = new QVBoxLayout;
    identity->setSpacing(2);
    auto *titleLabel = label(tr("Criar filtro rápido"), QStringLiteral("quickFilterTitle"), surface);
    mAccountLabel = label(accountName.isEmpty() ? tr("Filtro local do KMail") : tr("Mensagem de %1").arg(accountName),
                          QStringLiteral("quickFilterAccount"),
                          surface);
    identity->addWidget(titleLabel);
    identity->addWidget(mAccountLabel);
    header->addLayout(identity, 1);
    mStepLabel = label({}, QStringLiteral("quickFilterStep"), surface);
    mStepLabel->setWordWrap(false);
    header->addWidget(mStepLabel, 0, Qt::AlignTop);
    layout->addLayout(header);

    mPageTitleLabel = label({}, QStringLiteral("quickFilterPageTitle"), surface);
    mPageDescriptionLabel = label({}, QStringLiteral("quickFilterDescription"), surface);
    layout->addWidget(mPageTitleLabel);
    layout->addWidget(mPageDescriptionLabel);

    mPages = new QStackedWidget(surface);
    mPages->addWidget(createConditionsPage());
    mPages->addWidget(createActionPage());
    mPages->addWidget(createApplicationPage());
    layout->addWidget(mPages, 1);

    mStatusLabel = label({}, QStringLiteral("quickFilterStatus"), surface);
    mStatusLabel->hide();
    layout->addWidget(mStatusLabel);
    mHintLabel = label({}, QStringLiteral("quickFilterHint"), surface);
    layout->addWidget(mHintLabel);

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

    mConditionList = new QListWidget(page);
    mConditionList->setObjectName(QStringLiteral("quickFilterConditions"));
    prepareList(mConditionList);
    for (qsizetype row = 0; row < mConditions.size(); ++row) {
        const QuickFilter::Condition &condition = mConditions.at(row);
        auto *item = new QListWidgetItem(QuickFilter::conditionLabel(condition), mConditionList);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(condition.enabled ? Qt::Checked : Qt::Unchecked);
        setConditionPresentation(item, condition);
    }
    if (mConditionList->count() > 0) {
        mConditionList->setCurrentRow(0);
    }
    connect(mConditionList, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
        const int row = mConditionList->row(item);
        if (row >= 0 && row < mConditions.size()) {
            mConditions[row].enabled = item->checkState() == Qt::Checked;
            updateControls();
            Q_EMIT previewRequested();
        }
    });
    layout->addWidget(mConditionList, 1);

    mEditor = new QLineEdit(page);
    mEditor->setObjectName(QStringLiteral("quickFilterValueEditor"));
    mEditor->setPlaceholderText(tr("Editar valor da condição"));
    mEditor->hide();
    layout->addWidget(mEditor);
    return page;
}

QWidget *QuickFilterDialog::createActionPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins({});
    mActionList = new QListWidget(page);
    mActionList->setObjectName(QStringLiteral("quickFilterActions"));
    prepareList(mActionList);
    addChoiceRow(mActionList, tr("Excluir"), tr("Adiciona a tag deleted"), QStringLiteral("edit-delete"));
    addChoiceRow(mActionList, tr("Marcar como spam"), tr("Adiciona a tag spam"), QStringLiteral("mail-mark-junk"));
    addChoiceRow(mActionList, tr("Arquivar"), tr("Adiciona a tag archived"), QStringLiteral("folder-documents"));
    mActionList->setCurrentRow(0);
    connect(mActionList, &QListWidget::currentRowChanged, this, [this] {
        refreshRadioRows(mActionList);
    });
    connect(mActionList, &QListWidget::itemClicked, this, [this] {
        refreshRadioRows(mActionList);
    });
    refreshRadioRows(mActionList);
    layout->addWidget(mActionList, 1);
    return page;
}

QWidget *QuickFilterDialog::createApplicationPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins({});
    mApplicationList = new QListWidget(page);
    mApplicationList->setObjectName(QStringLiteral("quickFilterApplication"));
    prepareList(mApplicationList);
    addChoiceRow(mApplicationList,
                 tr("Mensagens desta pasta"),
                 tr("Aplica retroativamente às mensagens correspondentes"),
                 QStringLiteral("mail-folder-inbox"));
    addChoiceRow(mApplicationList,
                 tr("Mensagem atual"),
                 tr("Aplica agora à mensagem em foco"),
                 QStringLiteral("mail-message-new"));
    addChoiceRow(mApplicationList,
                 tr("Somente novas mensagens"),
                 tr("Não modifica as mensagens existentes"),
                 QStringLiteral("view-refresh"));
    mApplicationList->setFixedHeight(210);
    mApplicationList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    mApplicationList->setCurrentRow(1);
    connect(mApplicationList, &QListWidget::currentRowChanged, this, [this] {
        refreshRadioRows(mApplicationList);
        updateControls();
    });
    refreshRadioRows(mApplicationList);
    layout->addWidget(mApplicationList);

    mPreviewSummary = new QLabel(tr("Prévia: carregando mensagens da pasta…"), page);
    mPreviewSummary->setObjectName(QStringLiteral("quickFilterPreviewSummary"));
    layout->addWidget(mPreviewSummary);
    mPreviewList = new QListWidget(page);
    mPreviewList->setObjectName(QStringLiteral("quickFilterPreview"));
    prepareList(mPreviewList);
    mPreviewList->setSelectionMode(QAbstractItemView::NoSelection);
    mPreviewList->setFocusPolicy(Qt::NoFocus);
    layout->addWidget(mPreviewList, 1);
    mPreviewPageLabel = new QLabel(page);
    mPreviewPageLabel->setObjectName(QStringLiteral("quickFilterPreviewPage"));
    layout->addWidget(mPreviewPageLabel);
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
    updateControls();
}

void QuickFilterDialog::setPreviewError(const QString &error)
{
    mPreviewRows.clear();
    mPreviewTotal = -1;
    mPreviewSummary->setText(tr("Não foi possível calcular a prévia: %1").arg(error));
    mPreviewSummary->setStyleSheet(QStringLiteral("color: palette(link);"));
    refreshPreviewPage();
    updateControls();
}

void QuickFilterDialog::setBusy(bool busy, const QString &message)
{
    mBusy = busy;
    mPages->setEnabled(!busy);
    mStatusLabel->setVisible(!message.isEmpty());
    mStatusLabel->setText(message);
    updateControls();
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
    mStatusLabel->hide();
    mStatusLabel->setStyleSheet({});
    updateControls();
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
    updateControls();
}

void QuickFilterDialog::refreshConditionRow(int row)
{
    if (row >= 0 && row < mConditions.size()) {
        setConditionPresentation(mConditionList->item(row), mConditions.at(row));
    }
}

void QuickFilterDialog::refreshRadioRows(QListWidget *list)
{
    if (!list) {
        return;
    }
    for (int row = 0; row < list->count(); ++row) {
        QListWidgetItem *const item = list->item(row);
        item->setCheckState(row == list->currentRow() ? Qt::Checked : Qt::Unchecked);
    }
}

void QuickFilterDialog::refreshPreviewPage()
{
    mPreviewList->clear();
    const int pageCount = std::max(1, (static_cast<int>(mPreviewRows.size()) + previewPageSize - 1) / previewPageSize);
    const int begin = mPreviewPage * previewPageSize;
    const int end = std::min(begin + previewPageSize, static_cast<int>(mPreviewRows.size()));
    for (int index = begin; index < end; ++index) {
        const QString row = mPreviewRows.at(index);
        const QStringList parts = row.split(QRegularExpression(QStringLiteral("\\s+·\\s+")));
        auto *item = new QListWidgetItem(row, mPreviewList);
        item->setIcon(QIcon::fromTheme(QStringLiteral("mail-message-new")));
        if (parts.size() >= 3) {
            item->setData(PrimaryTextRole, parts.mid(2).join(QStringLiteral("  ·  ")));
            item->setData(SecondaryTextRole, QStringLiteral("%1  ·  %2").arg(parts.at(1), parts.at(0)));
        } else {
            item->setData(PrimaryTextRole, row);
        }
    }
    mPreviewPageLabel->setText(tr("Página %1/%2 — até %3 mensagens por página")
                                   .arg(mPreviewPage + 1)
                                   .arg(pageCount)
                                   .arg(previewPageSize));
}

void QuickFilterDialog::updateControls()
{
    updatePageChrome();
}

void QuickFilterDialog::updatePageChrome()
{
    if (!mPages) {
        return;
    }

    const int page = mPages->currentIndex();
    static const QStringList titles = {
        tr("Escolha as condições"),
        tr("Escolha a ação"),
        tr("Escolha quando aplicar"),
    };
    static const QStringList descriptions = {
        tr("As condições marcadas serão combinadas com AND."),
        tr("O filtro adiciona uma tag; use S para efetivar a operação."),
        tr("A regra será aplicada a novas mensagens em todas as Inboxes."),
    };
    static const QStringList hints = {
        tr("j/k  navegar    espaço  marcar    e  editar    ↵  continuar    esc  cancelar"),
        tr("j/k  navegar    espaço  escolher    ↵  continuar    esc  voltar"),
        tr("j/k  navegar    ctrl+d/u  página    ↵  criar filtro    esc  voltar"),
    };

    mStepLabel->setText(tr("%1 / 3").arg(page + 1));
    mPageTitleLabel->setText(titles.at(page));
    mPageDescriptionLabel->setText(descriptions.at(page));
    mHintLabel->setText(mBusy ? tr("Aguarde…") : hints.at(page));
}
