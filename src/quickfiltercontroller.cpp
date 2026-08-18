/*
    SPDX-FileCopyrightText: 2026 KMail Vim Navigation contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "quickfiltercontroller.h"

#include "quickfilterdialog.h"
#include "vimmessagemanager.h"

#include <Akonadi/AgentManager>
#include <Akonadi/ItemFetchJob>
#include <Akonadi/ItemFetchScope>
#include <Akonadi/MessageParts>
#include <Akonadi/SpecialMailCollections>
#include <Akonadi/Tag>

#include <MailCommon/FilterAction>
#include <MailCommon/FilterActionDict>
#include <MailCommon/FilterManager>
#include <MailCommon/MailFilter>
#include <MailCommon/SearchPattern>

#include <KMime/Message>

#include <KJob>

#include <QApplication>
#include <QDateTime>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <memory>

namespace
{
void configureHeaderFetch(Akonadi::ItemFetchScope &scope)
{
    scope.fetchPayloadPart(Akonadi::MessagePart::Envelope);
    scope.fetchPayloadPart(Akonadi::MessagePart::Header);
    scope.setFetchTags(true);
    scope.setAncestorRetrieval(Akonadi::ItemFetchScope::Parent);
}

std::shared_ptr<KMime::Message> messagePayload(const Akonadi::Item &item)
{
    return item.hasPayload<std::shared_ptr<KMime::Message>>() ? item.payload<std::shared_ptr<KMime::Message>>() : nullptr;
}

QDateTime messageDate(const Akonadi::Item &item)
{
    const auto message = messagePayload(item);
    const auto *date = message ? message->date() : nullptr;
    return date ? date->dateTime() : QDateTime();
}

QString messageSubject(const Akonadi::Item &item)
{
    const auto message = messagePayload(item);
    const auto *subject = message ? message->subject() : nullptr;
    return subject ? subject->asUnicodeString().simplified() : QString();
}

QString messageSender(const Akonadi::Item &item)
{
    const auto message = messagePayload(item);
    const auto *from = message ? message->from() : nullptr;
    if (!from) {
        return {};
    }
    const auto mailboxes = from->mailboxes();
    return mailboxes.isEmpty() ? QString() : mailboxes.constFirst().prettyAddress().simplified();
}

bool isTrashFolder(const Akonadi::Collection &folder)
{
    if (Akonadi::SpecialMailCollections::specialCollectionType(folder) == Akonadi::SpecialMailCollections::Trash) {
        return true;
    }
    if (folder.resource().isEmpty()) {
        return false;
    }
    const Akonadi::AgentInstance account = Akonadi::AgentManager::self()->instance(folder.resource());
    const Akonadi::Collection trash = account.isValid()
        ? Akonadi::SpecialMailCollections::self()->collection(Akonadi::SpecialMailCollections::Trash, account)
        : Akonadi::Collection();
    return trash.isValid() && trash.id() == folder.id();
}
}

QuickFilterController::QuickFilterController(VimMessageManager *messageManager, QObject *parent)
    : QObject(parent)
    , mMessageManager(messageManager)
{
}

bool QuickFilterController::isOpen() const
{
    return mOpening || !mDialog.isNull();
}

void QuickFilterController::open(Akonadi::Item::Id currentItemId,
                                 const Akonadi::Collection &currentFolder,
                                 QWidget *parentWidget)
{
    if (mDialog) {
        mDialog->raise();
        mDialog->activateWindow();
        return;
    }
    if (currentItemId <= 0) {
        Q_EMIT statusMessage(tr("Nenhuma mensagem atual foi encontrada para criar o filtro."));
        return;
    }
    if (!currentFolder.isValid()) {
        Q_EMIT statusMessage(tr("Nenhuma pasta de mensagens está aberta."));
        return;
    }
    if (nativeFilterDialogIsOpen()) {
        Q_EMIT statusMessage(tr("Feche o gerenciador de filtros do KMail antes de criar um filtro rápido."));
        return;
    }

    mCurrentFolder = currentFolder;
    mOpening = true;
    const quint64 generation = ++mGeneration;
    Q_EMIT stateChanged();
    auto *fetchJob = new Akonadi::ItemFetchJob(Akonadi::Item(currentItemId), this);
    configureHeaderFetch(fetchJob->fetchScope());
    connect(fetchJob, &Akonadi::ItemFetchJob::result, this, [this, fetchJob, parentWidget, generation](KJob *job) {
        if (generation != mGeneration) {
            return;
        }
        if (job->error() || fetchJob->items().isEmpty()) {
            const QString detail = job->error() ? job->errorString() : tr("mensagem não encontrada");
            reset();
            Q_EMIT statusMessage(tr("Não foi possível carregar a mensagem atual: %1").arg(detail));
            return;
        }

        mSourceItem = fetchJob->items().constFirst();
        const auto message = messagePayload(mSourceItem);
        const QList<QuickFilter::Condition> conditions = QuickFilter::conditionsFromMessage(message);
        if (conditions.isEmpty()) {
            reset();
            Q_EMIT statusMessage(tr("A mensagem atual não possui remetente, List-Id ou assunto utilizável."));
            return;
        }

        QString accountName;
        if (!mCurrentFolder.resource().isEmpty()) {
            const Akonadi::AgentInstance account = Akonadi::AgentManager::self()->instance(mCurrentFolder.resource());
            if (account.isValid()) {
                accountName = account.name();
            }
        }
        if (accountName.isEmpty()) {
            accountName = mCurrentFolder.name();
        }

        mDialog = new QuickFilterDialog(accountName, conditions, parentWidget);
        mOpening = false;
        connect(mDialog, &QuickFilterDialog::previewRequested, this, &QuickFilterController::updatePreview);
        connect(mDialog, &QuickFilterDialog::finishRequested, this, &QuickFilterController::finishRequested);
        connect(mDialog, &QDialog::finished, this, [this] {
            reset();
        });
        mDialog->show();
        fetchFolderMessages(mCurrentFolder);
    });
}

void QuickFilterController::fetchFolderMessages(const Akonadi::Collection &folder)
{
    const quint64 generation = mGeneration;
    mFolderFetchFinished = false;
    mPreviewError.clear();
    mFolderItems.clear();
    auto *fetchJob = new Akonadi::ItemFetchJob(folder, this);
    configureHeaderFetch(fetchJob->fetchScope());
    connect(fetchJob, &Akonadi::ItemFetchJob::result, this, [this, fetchJob, generation](KJob *job) {
        if (generation != mGeneration) {
            return;
        }
        mFolderFetchFinished = true;
        if (job->error()) {
            mPreviewError = job->errorString();
        } else {
            for (const Akonadi::Item &item : fetchJob->items()) {
                if (item.mimeType() == QStringLiteral("message/rfc822") && messagePayload(item)) {
                    mFolderItems.push_back(item);
                }
            }
            std::sort(mFolderItems.begin(), mFolderItems.end(), [](const Akonadi::Item &left, const Akonadi::Item &right) {
                return messageDate(left) > messageDate(right);
            });
        }
        updatePreview();
    });
}

void QuickFilterController::updatePreview()
{
    if (!mDialog || !mFolderFetchFinished) {
        return;
    }
    if (!mPreviewError.isEmpty()) {
        mDialog->setPreviewError(mPreviewError);
        return;
    }

    const QList<QuickFilter::Condition> conditions = mDialog->selectedConditions();
    mMatchingItems.clear();
    QStringList rows;
    for (const Akonadi::Item &item : std::as_const(mFolderItems)) {
        if (QuickFilter::matches(conditions, item)) {
            mMatchingItems.push_back(item);
            rows.push_back(previewRow(item));
        }
    }
    mDialog->setPreview(rows, mMatchingItems.size());
}

void QuickFilterController::finishRequested()
{
    if (!mDialog || mSaving) {
        return;
    }
    if (nativeFilterDialogIsOpen()) {
        fail(tr("Feche o gerenciador de filtros do KMail antes de salvar. Ele mantém uma cópia antiga da lista de filtros."));
        return;
    }

    PendingDraft draft;
    draft.conditions = mDialog->selectedConditions();
    draft.action = mDialog->workflowAction();
    draft.existingMessages = mDialog->existingMessagesMode();
    if (draft.conditions.isEmpty()) {
        fail(tr("Marque ao menos uma condição."));
        return;
    }
    if (draft.existingMessages == QuickFilter::ExistingMessages::CurrentFolder && !mFolderFetchFinished) {
        fail(tr("Aguarde a conclusão da prévia antes da aplicação retroativa."));
        return;
    }
    if (draft.existingMessages == QuickFilter::ExistingMessages::CurrentMessage
        && !QuickFilter::matches(draft.conditions, mSourceItem)) {
        fail(tr("Após a edição, a mensagem atual não satisfaz todas as condições do filtro."));
        return;
    }
    if (draft.action == QuickFilter::WorkflowAction::Deleted
        && draft.existingMessages != QuickFilter::ExistingMessages::FutureOnly && isTrashFolder(mCurrentFolder)) {
        fail(tr("A aplicação imediata de 'deleted' foi bloqueada na Lixeira, pois S excluiria essas mensagens permanentemente."));
        return;
    }

    mSaving = true;
    mDialog->setBusy(true, tr("Preparando a tag e o agente de filtros…"));
    const QString tagName = QuickFilter::workflowTagName(draft.action);
    mMessageManager->resolveRequiredTag(
        tagName,
        [this, draft](const Akonadi::Tag &tag, const QString &tagError) {
            if (!tagError.isEmpty()) {
                fail(tagError);
                return;
            }
            waitForFilterBackend(draft, tag);
        });
}

void QuickFilterController::waitForFilterBackend(const PendingDraft &draft, const Akonadi::Tag &tag)
{
    if (!mDialog) {
        return;
    }
    MailCommon::FilterManager *const manager = MailCommon::FilterManager::instance();
    if (manager->initialized() && manager->isValid() && manager->tagList().contains(tag.url())) {
        saveFilter(draft, tag, manager);
        return;
    }
    if (manager->initialized() && !manager->isValid()) {
        fail(tr("O agente de filtros do KMail não está disponível."));
        return;
    }

    auto completed = std::make_shared<bool>(false);
    auto *timeout = new QTimer(this);
    timeout->setSingleShot(true);
    timeout->setInterval(15000);
    const auto tryAgain = [this, draft, tag, manager, completed, timeout] {
        if (*completed || !mDialog) {
            return;
        }
        if (manager->initialized() && manager->isValid() && manager->tagList().contains(tag.url())) {
            *completed = true;
            timeout->stop();
            timeout->deleteLater();
            saveFilter(draft, tag, manager);
        } else if (manager->initialized() && !manager->isValid()) {
            *completed = true;
            timeout->stop();
            timeout->deleteLater();
            fail(tr("O agente de filtros do KMail não está disponível."));
        }
    };
    connect(manager, &MailCommon::FilterManager::loadingFiltersDone, this, tryAgain, Qt::SingleShotConnection);
    connect(manager, &MailCommon::FilterManager::tagListingFinished, this, tryAgain, Qt::SingleShotConnection);
    connect(timeout, &QTimer::timeout, this, [this, completed, timeout] {
        timeout->deleteLater();
        if (!*completed && mDialog) {
            *completed = true;
            fail(tr("O KMail não disponibilizou a lista de tags ao agente de filtros."));
        }
    });
    timeout->start();
}

void QuickFilterController::saveFilter(const PendingDraft &draft,
                                       const Akonadi::Tag &tag,
                                       MailCommon::FilterManager *manager)
{
    if (!mDialog) {
        return;
    }
    if (nativeFilterDialogIsOpen()) {
        fail(tr("Feche o gerenciador de filtros do KMail antes de salvar."));
        return;
    }

    QString filterError;
    std::unique_ptr<MailCommon::MailFilter> filter(buildFilter(draft, tag, manager, &filterError));
    if (!filter) {
        fail(filterError);
        return;
    }
    const QString filterName = filter->name();
    const QString identifier = filter->identifier();

    QList<MailCommon::MailFilter *> updatedFilters;
    updatedFilters.reserve(manager->filters().size() + 1);
    updatedFilters.push_back(filter.release());
    for (const MailCommon::MailFilter *existing : manager->filters()) {
        updatedFilters.push_back(new MailCommon::MailFilter(*existing));
    }
    manager->setFilters(updatedFilters);

    const QList<MailCommon::MailFilter *> persistedFilters = manager->filters();
    const bool persistedInManager = std::any_of(persistedFilters.cbegin(), persistedFilters.cend(), [&identifier](const auto *candidate) {
        return candidate && candidate->identifier() == identifier;
    });
    if (!persistedInManager) {
        fail(tr("O KMail não confirmou o cadastro do filtro."));
        return;
    }

    applyToExistingMessages(draft, tag.name(), filterName);
}

MailCommon::MailFilter *QuickFilterController::buildFilter(const PendingDraft &draft,
                                                           const Akonadi::Tag &tag,
                                                           MailCommon::FilterManager *manager,
                                                           QString *error) const
{
    auto filter = std::make_unique<MailCommon::MailFilter>();
    filter->generateRandomIdentifier();
    filter->pattern()->clear();
    filter->pattern()->setOp(MailCommon::SearchPattern::OpAnd);
    const QString name = manager->createUniqueFilterName(QuickFilter::filterBaseName(draft.conditions, draft.action));
    filter->pattern()->setName(name);
    for (const QuickFilter::Condition &condition : draft.conditions) {
        const auto rule = MailCommon::SearchRule::createInstance(condition.field,
                                                                 condition.function,
                                                                 QuickFilter::searchContents(condition));
        if (!rule || rule->isEmpty()) {
            if (error) {
                *error = tr("Uma das condições do filtro é inválida.");
            }
            return nullptr;
        }
        filter->pattern()->append(rule);
    }

    const auto *description = MailCommon::FilterManager::filterActionDict()->value(QStringLiteral("add tag"));
    if (!description || !description->create) {
        if (error) {
            *error = tr("Esta versão do KMail não oferece a ação de filtro 'adicionar tag'.");
        }
        return nullptr;
    }
    MailCommon::FilterAction *const action = description->create();
    action->argsFromString(tag.url().toString());
    const Akonadi::Tag configuredTag = Akonadi::Tag::fromUrl(QUrl(action->argsAsString()));
    if (!configuredTag.isValid() || configuredTag.id() != tag.id()) {
        delete action;
        if (error) {
            *error = tr("O KMail não reconheceu a tag '%1' na ação do filtro.").arg(tag.name());
        }
        return nullptr;
    }
    filter->actions()->append(action);

    filter->setEnabled(true);
    filter->setApplyOnInbound(true);
    filter->setApplyOnAllFoldersInbound(false);
    filter->setApplyOnExplicit(false);
    filter->setApplyOnOutbound(false);
    filter->setApplyBeforeOutbound(false);
    filter->setApplicability(MailCommon::MailFilter::All);
    filter->clearApplyOnAccount();
    filter->setStopProcessingHere(false);
    filter->setConfigureShortcut(false);
    filter->setConfigureToolbar(false);
    filter->setAutoNaming(false);
    filter->setIcon(draft.action == QuickFilter::WorkflowAction::Deleted
                        ? QStringLiteral("edit-delete")
                        : draft.action == QuickFilter::WorkflowAction::Spam ? QStringLiteral("mail-mark-junk")
                                                                           : QStringLiteral("mail-archive"));

    const QString validationError = filter->purify();
    if (!validationError.isEmpty() || filter->isEmpty() || filter->actions()->isEmpty()) {
        if (error) {
            *error = validationError.isEmpty() ? tr("O rascunho do filtro é inválido.") : validationError;
        }
        return nullptr;
    }
    return filter.release();
}

void QuickFilterController::applyToExistingMessages(const PendingDraft &draft,
                                                     const QString &tagName,
                                                     const QString &filterName)
{
    Akonadi::Item::List targets;
    switch (draft.existingMessages) {
    case QuickFilter::ExistingMessages::CurrentFolder:
        targets = mMatchingItems;
        break;
    case QuickFilter::ExistingMessages::CurrentMessage:
        targets = {mSourceItem};
        break;
    case QuickFilter::ExistingMessages::FutureOnly:
        if (mDialog) {
            mDialog->complete(tr("Filtro '%1' criado para novas mensagens nas Inboxes.").arg(filterName));
        }
        Q_EMIT statusMessage(tr("Filtro rápido criado: %1").arg(filterName));
        return;
    }

    if (targets.isEmpty()) {
        if (mDialog) {
            mDialog->complete(tr("Filtro '%1' criado; nenhuma mensagem existente corresponde à regra.").arg(filterName));
        }
        Q_EMIT statusMessage(tr("Filtro rápido criado: %1").arg(filterName));
        return;
    }

    if (mDialog) {
        mDialog->setBusy(true, tr("Filtro criado. Aplicando a tag às mensagens existentes…"));
    }
    mMessageManager->addRequiredTagToItems(
        tagName,
        targets,
        [this, filterName, targetCount = targets.size()](int changedCount, const QString &applyError) {
            if (!applyError.isEmpty()) {
                const QString warning = tr("O filtro '%1' foi criado, mas a aplicação às mensagens existentes falhou: %2")
                                            .arg(filterName, applyError);
                if (mDialog) {
                    mDialog->complete(warning);
                }
                Q_EMIT statusMessage(warning);
                return;
            }
            const QString message = tr("Filtro '%1' criado; tag adicionada a %2 de %3 mensagem(ns). Use S para efetivar a ação.")
                                        .arg(filterName)
                                        .arg(changedCount)
                                        .arg(targetCount);
            if (mDialog) {
                mDialog->complete(message);
            }
            Q_EMIT statusMessage(message);
        });
}

bool QuickFilterController::nativeFilterDialogIsOpen() const
{
    const auto topLevels = QApplication::topLevelWidgets();
    return std::any_of(topLevels.cbegin(), topLevels.cend(), [](const QWidget *widget) {
        return widget && widget->isVisible() && widget->objectName() == QStringLiteral("filterdialog");
    });
}

QString QuickFilterController::previewRow(const Akonadi::Item &item) const
{
    const QDateTime date = messageDate(item);
    const QString dateText = date.isValid() ? date.toLocalTime().toString(QStringLiteral("yyyy-MM-dd")) : QStringLiteral("—");
    const QString sender = messageSender(item).isEmpty() ? tr("remetente desconhecido") : messageSender(item);
    const QString subject = messageSubject(item).isEmpty() ? tr("(sem assunto)") : messageSubject(item);
    return QStringLiteral("%1  ·  %2  ·  %3").arg(dateText, sender, subject);
}

void QuickFilterController::fail(const QString &error)
{
    mSaving = false;
    mOpening = false;
    if (mDialog) {
        mDialog->showError(error);
    }
    Q_EMIT statusMessage(error);
}

void QuickFilterController::reset()
{
    ++mGeneration;
    if (mDialog) {
        mDialog->deleteLater();
    }
    mDialog = nullptr;
    mSourceItem = {};
    mCurrentFolder = {};
    mFolderItems.clear();
    mMatchingItems.clear();
    mPreviewError.clear();
    mFolderFetchFinished = false;
    mSaving = false;
    mOpening = false;
    Q_EMIT stateChanged();
}
