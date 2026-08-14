/*
    SPDX-FileCopyrightText: 2026 KMail Vim Navigation contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "vimmessagemanager.h"

#include <Akonadi/CollectionCreateJob>
#include <Akonadi/CollectionFetchJob>
#include <Akonadi/ItemFetchJob>
#include <Akonadi/ItemFetchScope>
#include <Akonadi/ItemModifyJob>
#include <Akonadi/ItemMoveJob>
#include <Akonadi/MessageFlags>
#include <Akonadi/TagCreateJob>
#include <Akonadi/TagFetchJob>
#include <Akonadi/TagFetchScope>
#include <Akonadi/TrashJob>

#include <KConfig>
#include <KConfigGroup>
#include <KJob>

#include <QDate>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <utility>

namespace
{
const QString deletedTagName = QStringLiteral("deleted");
const QString archivedTagName = QStringLiteral("archived");
const QString spamTagName = QStringLiteral("spam");

bool isWorkflowTag(const Akonadi::Tag &tag)
{
    const QString name = tag.name();
    return name == deletedTagName || name == archivedTagName || name == spamTagName;
}

bool hasTagNamed(const Akonadi::Item &item, const QString &name)
{
    const auto tags = item.tags();
    return std::any_of(tags.cbegin(), tags.cend(), [&name](const Akonadi::Tag &tag) {
        return tag.name() == name;
    });
}

void clearWorkflowTagsFromItem(Akonadi::Item &item)
{
    const auto tags = item.tags();
    for (const Akonadi::Tag &tag : tags) {
        if (isWorkflowTag(tag)) {
            item.clearTag(tag);
        }
    }
}

QString joinedErrors(const QStringList &errors)
{
    QStringList uniqueErrors = errors;
    uniqueErrors.removeDuplicates();
    return uniqueErrors.join(QStringLiteral("; "));
}
}

VimMessageManager::VimMessageManager(QObject *parent)
    : QObject(parent)
{
}

bool VimMessageManager::isBusy() const
{
    return mBusy;
}

bool VimMessageManager::canUndo() const
{
    return mUndoTag.isValid() && !mUndoItems.isEmpty();
}

void VimMessageManager::setBusy(bool busy)
{
    if (mBusy == busy) {
        return;
    }
    mBusy = busy;
    Q_EMIT stateChanged();
}

void VimMessageManager::finishCommand(const QString &message)
{
    setBusy(false);
    if (!message.isEmpty()) {
        Q_EMIT statusMessage(message);
    }
}

void VimMessageManager::resolveTag(const QString &tagName, TagCallback callback)
{
    auto *fetchJob = new Akonadi::TagFetchJob(this);
    fetchJob->fetchScope().setFetchIdOnly(false);
    connect(fetchJob, &Akonadi::TagFetchJob::result, this, [this, fetchJob, tagName, callback = std::move(callback)](KJob *job) mutable {
        if (job->error()) {
            callback({}, tr("Não foi possível consultar as tags: %1").arg(job->errorString()));
            return;
        }

        const auto tags = fetchJob->tags();
        const auto existing = std::find_if(tags.cbegin(), tags.cend(), [&tagName](const Akonadi::Tag &tag) {
            return tag.name() == tagName;
        });
        if (existing != tags.cend()) {
            callback(*existing, {});
            return;
        }

        // A PLAIN tag has a stable GID equal to its visible name. Merge protects
        // against two KMail windows creating the same workflow tag at once.
        auto *createJob = new Akonadi::TagCreateJob(Akonadi::Tag(tagName), this);
        createJob->setMergeIfExisting(true);
        connect(createJob, &Akonadi::TagCreateJob::result, this, [createJob, callback = std::move(callback)](KJob *createResult) mutable {
            if (createResult->error()) {
                callback({}, QObject::tr("Não foi possível criar a tag: %1").arg(createResult->errorString()));
                return;
            }
            callback(createJob->tag(), {});
        });
    });
}

void VimMessageManager::fetchItemsWithTags(const Akonadi::Item::List &items, FetchCallback callback)
{
    auto *fetchJob = new Akonadi::ItemFetchJob(items, this);
    fetchJob->fetchScope().setFetchTags(true);
    fetchJob->fetchScope().tagFetchScope().setFetchIdOnly(false);
    fetchJob->fetchScope().setAncestorRetrieval(Akonadi::ItemFetchScope::Parent);
    connect(fetchJob, &Akonadi::ItemFetchJob::result, this, [fetchJob, callback = std::move(callback)](KJob *job) mutable {
        if (job->error()) {
            callback({}, QObject::tr("Não foi possível carregar as mensagens: %1").arg(job->errorString()));
            return;
        }
        callback(fetchJob->items(), {});
    });
}

void VimMessageManager::registerTagForDisplay(const Akonadi::Tag &tag)
{
    KConfig config(QStringLiteral("kmail2rc"));
    KConfigGroup group(&config, QStringLiteral("MessageListView"));
    QStringList selectedTags = group.readEntry(QStringLiteral("TagSelected")).split(QLatin1Char(','), Qt::SkipEmptyParts);
    const QString tagUrl = tag.url().url();
    if (!selectedTags.contains(tagUrl)) {
        selectedTags.push_back(tagUrl);
        group.writeEntry(QStringLiteral("TagSelected"), selectedTags);
        group.sync();
    }
    Q_EMIT tagDisplayChanged();
}

void VimMessageManager::assignTag(const QString &tagName, const Akonadi::Item::List &items)
{
    if (mBusy || items.isEmpty()) {
        return;
    }

    setBusy(true);
    resolveTag(tagName, [this, tagName, items](const Akonadi::Tag &tag, const QString &tagError) {
        if (!tagError.isEmpty()) {
            finishCommand(tagError);
            return;
        }

        fetchItemsWithTags(items, [this, tagName, tag](const Akonadi::Item::List &fetchedItems, const QString &fetchError) {
            if (!fetchError.isEmpty()) {
                finishCommand(fetchError);
                return;
            }

            Akonadi::Item::List changedItems;
            changedItems.reserve(fetchedItems.size());
            for (const Akonadi::Item &source : fetchedItems) {
                if (!source.hasTag(tag)) {
                    Akonadi::Item item(source);
                    item.setTag(tag);
                    changedItems.push_back(item);
                }
            }

            if (changedItems.isEmpty()) {
                registerTagForDisplay(tag);
                finishCommand(tr("As mensagens selecionadas já possuem a tag '%1'.").arg(tagName));
                return;
            }

            auto *modifyJob = new Akonadi::ItemModifyJob(changedItems, this);
            modifyJob->setIgnorePayload(true);
            modifyJob->disableRevisionCheck();
            connect(modifyJob, &Akonadi::ItemModifyJob::result, this, [this, changedItems, tag, tagName](KJob *job) {
                if (job->error()) {
                    finishCommand(tr("Não foi possível atribuir a tag '%1': %2").arg(tagName, job->errorString()));
                    return;
                }

                // Do not report success solely from the STORE response: fetch
                // the items again and verify that Akonadi persisted the tag.
                fetchItemsWithTags(changedItems, [this, tag, tagName](const Akonadi::Item::List &verifiedItems, const QString &fetchError) {
                    if (!fetchError.isEmpty()) {
                        finishCommand(fetchError);
                        return;
                    }

                    const auto missingTag = std::find_if(verifiedItems.cbegin(), verifiedItems.cend(), [&tag](const Akonadi::Item &item) {
                        return !item.hasTag(tag);
                    });
                    if (verifiedItems.isEmpty() || missingTag != verifiedItems.cend()) {
                        finishCommand(tr("O Akonadi não confirmou a atribuição da tag '%1'.").arg(tagName));
                        return;
                    }

                    mUndoTag = tag;
                    mUndoItems = verifiedItems;
                    registerTagForDisplay(tag);
                    Q_EMIT stateChanged();
                    finishCommand(tr("Tag '%1' atribuída a %2 mensagem(ns).").arg(tagName).arg(mUndoItems.size()));
                });
            });
        });
    });
}

void VimMessageManager::undoLastTagAssignment()
{
    if (mBusy || !canUndo()) {
        return;
    }

    setBusy(true);
    const QString tagName = mUndoTag.name();
    Akonadi::Item::List items = mUndoItems;
    for (Akonadi::Item &item : items) {
        item.clearTag(mUndoTag);
    }

    auto *modifyJob = new Akonadi::ItemModifyJob(items, this);
    modifyJob->setIgnorePayload(true);
    modifyJob->disableRevisionCheck();
    connect(modifyJob, &Akonadi::ItemModifyJob::result, this, [this, tagName](KJob *job) {
        if (job->error()) {
            finishCommand(tr("Não foi possível desfazer a tag '%1': %2").arg(tagName, job->errorString()));
            return;
        }

        mUndoTag = {};
        mUndoItems.clear();
        Q_EMIT stateChanged();
        finishCommand(tr("A atribuição da tag '%1' foi desfeita.").arg(tagName));
    });
}

void VimMessageManager::applyTaggedActions(const Akonadi::Item::List &items)
{
    if (mBusy || items.isEmpty()) {
        return;
    }

    setBusy(true);
    fetchItemsWithTags(items, [this](const Akonadi::Item::List &fetchedItems, const QString &fetchError) {
        if (!fetchError.isEmpty()) {
            finishCommand(fetchError);
            return;
        }

        Akonadi::Item::List deletedItems;
        Akonadi::Item::List spamItems;
        Akonadi::Item::List archivedItems;
        for (const Akonadi::Item &item : fetchedItems) {
            // A message can carry more than one workflow tag. Destructive
            // actions win, so a message is never moved twice in one apply.
            if (hasTagNamed(item, deletedTagName)) {
                deletedItems.push_back(item);
            } else if (hasTagNamed(item, spamTagName)) {
                spamItems.push_back(item);
            } else if (hasTagNamed(item, archivedTagName)) {
                archivedItems.push_back(item);
            }
        }

        mApplyMessageCount = deletedItems.size() + spamItems.size() + archivedItems.size();
        mApplyErrors.clear();
        mPendingApplyOperations = (deletedItems.isEmpty() ? 0 : 1) + (spamItems.isEmpty() ? 0 : 1) + (archivedItems.isEmpty() ? 0 : 1);

        if (mPendingApplyOperations == 0) {
            finishCommand(tr("Nenhuma das mensagens selecionadas possui tags de ação."));
            return;
        }

        if (!deletedItems.isEmpty()) {
            startDeleteAction(deletedItems);
        }
        if (!spamItems.isEmpty()) {
            startSpamAction(spamItems);
        }
        if (!archivedItems.isEmpty()) {
            startArchiveActions(archivedItems);
        }
    });
}

void VimMessageManager::startDeleteAction(const Akonadi::Item::List &items)
{
    auto *trashJob = new Akonadi::TrashJob(items, this);
    connect(trashJob, &Akonadi::TrashJob::result, this, [this, items](KJob *job) {
        if (job->error()) {
            finishApplyOperation(tr("Falha ao mover mensagens para a lixeira: %1").arg(job->errorString()));
            return;
        }
        clearWorkflowTags(items, [this](const QString &error) {
            finishApplyOperation(error);
        });
    });
}

void VimMessageManager::startSpamAction(const Akonadi::Item::List &items)
{
    Akonadi::Item::List changedItems = items;
    for (Akonadi::Item &item : changedItems) {
        item.setFlag(Akonadi::MessageFlags::Spam);
        item.clearFlag(Akonadi::MessageFlags::Ham);
    }

    auto *modifyJob = new Akonadi::ItemModifyJob(changedItems, this);
    modifyJob->setIgnorePayload(true);
    modifyJob->disableRevisionCheck();
    connect(modifyJob, &Akonadi::ItemModifyJob::result, this, [this, items](KJob *job) {
        if (job->error()) {
            finishApplyOperation(tr("Falha ao marcar mensagens como spam: %1").arg(job->errorString()));
            return;
        }
        clearWorkflowTags(items, [this](const QString &error) {
            finishApplyOperation(error);
        });
    });
}

void VimMessageManager::startArchiveActions(const Akonadi::Item::List &items)
{
    QList<Akonadi::Collection::Id> parentIds;
    for (const Akonadi::Item &item : items) {
        const auto parentId = item.parentCollection().id();
        if (parentId > 0 && !parentIds.contains(parentId)) {
            parentIds.push_back(parentId);
        }
    }

    if (parentIds.isEmpty()) {
        finishApplyOperation(tr("Não foi possível identificar a conta das mensagens a arquivar."));
        return;
    }

    auto *fetchJob = new Akonadi::CollectionFetchJob(parentIds, Akonadi::CollectionFetchJob::Base, this);
    connect(fetchJob, &Akonadi::CollectionFetchJob::result, this, [this, fetchJob, items](KJob *job) {
        if (job->error()) {
            finishApplyOperation(tr("Não foi possível consultar as pastas de origem: %1").arg(job->errorString()));
            return;
        }

        QHash<Akonadi::Collection::Id, QString> resources;
        for (const Akonadi::Collection &collection : fetchJob->collections()) {
            resources.insert(collection.id(), collection.resource());
        }

        QHash<QString, Akonadi::Item::List> groupedItems;
        QStringList errors;
        for (const Akonadi::Item &item : items) {
            const QString resource = resources.value(item.parentCollection().id());
            if (resource.isEmpty()) {
                errors.push_back(tr("Conta não encontrada para a mensagem %1.").arg(item.id()));
            } else {
                groupedItems[resource].push_back(item);
            }
        }

        QList<ArchiveGroup> groups;
        groups.reserve(groupedItems.size());
        for (auto it = groupedItems.cbegin(); it != groupedItems.cend(); ++it) {
            groups.push_back({it.key(), it.value()});
        }

        if (groups.isEmpty()) {
            finishApplyOperation(joinedErrors(errors));
            return;
        }

        archiveGroups(groups, 0, errors, [this](const QString &error) {
            finishApplyOperation(error);
        });
    });
}

void VimMessageManager::archiveGroups(const QList<ArchiveGroup> &groups, qsizetype index, QStringList errors, ErrorCallback callback)
{
    if (index >= groups.size()) {
        callback(joinedErrors(errors));
        return;
    }

    const ArchiveGroup group = groups.at(index);
    resolveArchiveDestination(group.resource,
                              [this, groups, index, errors = std::move(errors), group, callback = std::move(callback)](
                                  const Akonadi::Collection &destination, const QString &destinationError) mutable {
                                  if (!destinationError.isEmpty()) {
                                      errors.push_back(destinationError);
                                      archiveGroups(groups, index + 1, std::move(errors), std::move(callback));
                                      return;
                                  }

                                  auto *moveJob = new Akonadi::ItemMoveJob(group.items, destination, this);
                                  connect(moveJob,
                                          &Akonadi::ItemMoveJob::result,
                                          this,
                                          [this, groups, index, errors = std::move(errors), group, callback = std::move(callback)](KJob *job) mutable {
                                              if (job->error()) {
                                                  errors.push_back(tr("Falha ao arquivar mensagens da conta %1: %2")
                                                                       .arg(group.resource, job->errorString()));
                                                  archiveGroups(groups, index + 1, std::move(errors), std::move(callback));
                                                  return;
                                              }

                                              clearWorkflowTags(group.items,
                                                                [this,
                                                                 groups,
                                                                 index,
                                                                 errors = std::move(errors),
                                                                 callback = std::move(callback)](const QString &clearError) mutable {
                                                                    if (!clearError.isEmpty()) {
                                                                        errors.push_back(clearError);
                                                                    }
                                                                    archiveGroups(groups,
                                                                                  index + 1,
                                                                                  std::move(errors),
                                                                                  std::move(callback));
                                                                });
                                          });
                              });
}

void VimMessageManager::resolveArchiveDestination(const QString &resource, CollectionCallback callback)
{
    KConfig config(QStringLiteral("foldermailarchiverc"));
    const KConfigGroup group = config.group(QStringLiteral("FolderArchiveAccount ") + resource);
    const bool enabled = group.readEntry("enabled", false);
    const Akonadi::Collection::Id topLevelId = group.readEntry("topLevelCollectionId", -1LL);
    const int archiveType = group.readEntry("folderArchiveType", 0);

    if (!enabled || topLevelId <= 0) {
        callback({}, tr("A pasta de arquivamento não está configurada para a conta %1.").arg(resource));
        return;
    }
    if (archiveType < 0 || archiveType > 2) {
        callback({}, tr("O tipo de arquivamento da conta %1 é inválido.").arg(resource));
        return;
    }

    if (archiveType == 0) {
        auto *fetchJob = new Akonadi::CollectionFetchJob(Akonadi::Collection(topLevelId), Akonadi::CollectionFetchJob::Base, this);
        connect(fetchJob,
                &Akonadi::CollectionFetchJob::result,
                this,
                [fetchJob, resource, callback = std::move(callback)](KJob *job) mutable {
                    if (job->error()) {
                        callback({}, QObject::tr("Não foi possível carregar a pasta de arquivamento da conta %1: %2")
                                         .arg(resource, job->errorString()));
                        return;
                    }
                    if (fetchJob->collections().isEmpty()) {
                        callback({}, QObject::tr("A pasta de arquivamento da conta %1 não existe.").arg(resource));
                        return;
                    }
                    const Akonadi::Collection collection = fetchJob->collections().constFirst();
                    if (!(collection.rights() & Akonadi::Collection::CanCreateItem)) {
                        callback({}, QObject::tr("A pasta de arquivamento da conta %1 é somente leitura.").arg(resource));
                        return;
                    }
                    callback(collection, {});
                });
        return;
    }

    const QDate currentDate = QDate::currentDate();
    const QString folderName = archiveType == 1 ? QStringLiteral("%1-%2").arg(currentDate.month()).arg(currentDate.year())
                                                : QString::number(currentDate.year());
    auto *fetchJob = new Akonadi::CollectionFetchJob(Akonadi::Collection(topLevelId), Akonadi::CollectionFetchJob::FirstLevel, this);
    connect(fetchJob,
            &Akonadi::CollectionFetchJob::result,
            this,
            [this, fetchJob, topLevelId, folderName, resource, callback = std::move(callback)](KJob *job) mutable {
                if (job->error()) {
                    callback({}, QObject::tr("Não foi possível carregar as pastas de arquivamento da conta %1: %2")
                                     .arg(resource, job->errorString()));
                    return;
                }

                for (const Akonadi::Collection &collection : fetchJob->collections()) {
                    if (collection.name() == folderName) {
                        if (!(collection.rights() & Akonadi::Collection::CanCreateItem)) {
                            callback({}, QObject::tr("A pasta de arquivamento da conta %1 é somente leitura.").arg(resource));
                        } else {
                            callback(collection, {});
                        }
                        return;
                    }
                }

                Akonadi::Collection collection;
                collection.setParentCollection(Akonadi::Collection(topLevelId));
                collection.setName(folderName);
                collection.setContentMimeTypes({QStringLiteral("message/rfc822")});
                auto *createJob = new Akonadi::CollectionCreateJob(collection, this);
                connect(createJob,
                        &Akonadi::CollectionCreateJob::result,
                        this,
                        [createJob, resource, callback = std::move(callback)](KJob *createResult) mutable {
                            if (createResult->error()) {
                                callback({}, QObject::tr("Não foi possível criar a pasta de arquivamento da conta %1: %2")
                                                 .arg(resource, createResult->errorString()));
                                return;
                            }
                            callback(createJob->collection(), {});
                        });
            });
}

void VimMessageManager::clearWorkflowTags(const Akonadi::Item::List &items, ErrorCallback callback)
{
    Akonadi::Item::List changedItems = items;
    for (Akonadi::Item &item : changedItems) {
        clearWorkflowTagsFromItem(item);
    }

    auto *modifyJob = new Akonadi::ItemModifyJob(changedItems, this);
    modifyJob->setIgnorePayload(true);
    modifyJob->disableRevisionCheck();
    connect(modifyJob, &Akonadi::ItemModifyJob::result, this, [callback = std::move(callback)](KJob *job) mutable {
        if (job->error()) {
            callback(QObject::tr("A ação foi executada, mas não foi possível remover as tags: %1").arg(job->errorString()));
            return;
        }
        callback({});
    });
}

void VimMessageManager::finishApplyOperation(const QString &error)
{
    if (!error.isEmpty()) {
        mApplyErrors.push_back(error);
    }

    --mPendingApplyOperations;
    if (mPendingApplyOperations > 0) {
        return;
    }

    if (mApplyErrors.isEmpty()) {
        finishCommand(tr("Ações aplicadas a %1 mensagem(ns).").arg(mApplyMessageCount));
    } else {
        finishCommand(tr("Aplicação concluída com erros: %1").arg(joinedErrors(mApplyErrors)));
    }
}
