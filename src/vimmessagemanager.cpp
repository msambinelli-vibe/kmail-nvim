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
#include <QTimer>

#include <algorithm>
#include <memory>
#include <utility>

namespace
{
const QString selectedTagName = QStringLiteral("selected");
const QString deletedTagName = QStringLiteral("deleted");
const QString archivedTagName = QStringLiteral("archived");
const QString spamTagName = QStringLiteral("spam");

const QStringList &requiredTagNames()
{
    static const QStringList names = {selectedTagName, deletedTagName, archivedTagName, spamTagName};
    return names;
}

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

QString joinedErrors(const QStringList &errors)
{
    QStringList uniqueErrors = errors;
    uniqueErrors.removeDuplicates();
    return uniqueErrors.join(QStringLiteral("; "));
}

Akonadi::Item::List uniqueValidItems(const Akonadi::Item::List &items)
{
    Akonadi::Item::List result;
    result.reserve(items.size());
    QSet<Akonadi::Item::Id> seen;
    for (const Akonadi::Item &item : items) {
        if (item.id() > 0 && !seen.contains(item.id())) {
            seen.insert(item.id());
            result.push_back(item);
        }
    }
    return result;
}

Akonadi::Tag tagNamed(const Akonadi::Tag::List &tags, const QString &name)
{
    Akonadi::Tag result;
    for (const Akonadi::Tag &tag : tags) {
        if (tag.name() == name && (!result.isValid() || tag.id() < result.id())) {
            // Prefer the oldest tag if a previous version or another client
            // accidentally created duplicate display names.
            result = tag;
        }
    }
    return result;
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

void VimMessageManager::ensureRequiredTags()
{
    if (mTagInitializationInProgress) {
        return;
    }

    const bool allCached = std::all_of(requiredTagNames().cbegin(), requiredTagNames().cend(), [this](const QString &tagName) {
        const auto cached = mTags.constFind(tagName);
        return cached != mTags.cend() && cached->isValid();
    });
    if (allCached) {
        return;
    }

    mTagInitializationInProgress = true;
    ++mTagInitializationAttempts;
    mPendingTagInitializations = 0;
    mTagInitializationErrors.clear();

    // Look up by display name before creating: tags created by other clients
    // may be GENERIC and therefore have a UUID instead of their name as GID.
    auto *fetchJob = new Akonadi::TagFetchJob(this);
    fetchJob->fetchScope().setFetchIdOnly(false);
    connect(fetchJob, &Akonadi::TagFetchJob::result, this, [this, fetchJob](KJob *job) {
        if (job->error()) {
            mTagInitializationErrors.push_back(tr("consulta de tags: %1").arg(job->errorString()));
            finishRequiredTagInitialization();
            return;
        }

        QStringList missingTags;
        const Akonadi::Tag::List tags = fetchJob->tags();
        for (const QString &tagName : requiredTagNames()) {
            const auto cached = mTags.constFind(tagName);
            const Akonadi::Tag existing = cached != mTags.cend() && cached->isValid() ? *cached : tagNamed(tags, tagName);
            if (existing.isValid()) {
                mTags.insert(tagName, existing);
                registerTagForDisplay(existing);
            } else {
                missingTags.push_back(tagName);
            }
        }

        if (missingTags.isEmpty()) {
            finishRequiredTagInitialization();
            return;
        }

        // Merge makes creation idempotent and closes the race between KMail
        // windows that completed the lookup at the same time.
        mPendingTagInitializations = missingTags.size();
        for (const QString &tagName : missingTags) {
            auto *createJob = new Akonadi::TagCreateJob(Akonadi::Tag(tagName), this);
            createJob->setMergeIfExisting(true);
            connect(createJob, &Akonadi::TagCreateJob::result, this, [this, createJob, tagName](KJob *createResult) {
                if (createResult->error()) {
                    mTagInitializationErrors.push_back(tr("%1: %2").arg(tagName, createResult->errorString()));
                } else {
                    const Akonadi::Tag tag = createJob->tag();
                    mTags.insert(tagName, tag);
                    registerTagForDisplay(tag);
                }

                --mPendingTagInitializations;
                if (mPendingTagInitializations == 0) {
                    finishRequiredTagInitialization();
                }
            });
        }
    });
}

void VimMessageManager::finishRequiredTagInitialization()
{
    mTagInitializationInProgress = false;
    if (mTagInitializationErrors.isEmpty()) {
        return;
    }
    if (mTagInitializationAttempts < 3) {
        QTimer::singleShot(2000, this, &VimMessageManager::ensureRequiredTags);
    } else {
        Q_EMIT statusMessage(tr("Não foi possível inicializar todas as tags do plugin: %1")
                                 .arg(joinedErrors(mTagInitializationErrors)));
    }
}

void VimMessageManager::resolveTag(const QString &tagName, TagCallback callback)
{
    const auto cached = mTags.constFind(tagName);
    if (cached != mTags.cend() && cached->isValid()) {
        callback(*cached, {});
        return;
    }

    auto *fetchJob = new Akonadi::TagFetchJob(this);
    fetchJob->fetchScope().setFetchIdOnly(false);
    connect(fetchJob,
            &Akonadi::TagFetchJob::result,
            this,
            [this, fetchJob, tagName, callback = std::move(callback)](KJob *job) mutable {
                if (job->error()) {
                    callback({}, tr("Não foi possível consultar as tags: %1").arg(job->errorString()));
                    return;
                }

                const Akonadi::Tag existing = tagNamed(fetchJob->tags(), tagName);
                if (existing.isValid()) {
                    mTags.insert(tagName, existing);
                    callback(existing, {});
                    return;
                }

                auto *createJob = new Akonadi::TagCreateJob(Akonadi::Tag(tagName), this);
                createJob->setMergeIfExisting(true);
                connect(createJob,
                        &Akonadi::TagCreateJob::result,
                        this,
                        [this, createJob, tagName, callback = std::move(callback)](KJob *createResult) mutable {
                            if (createResult->error()) {
                                callback({}, tr("Não foi possível criar a tag '%1': %2")
                                                 .arg(tagName, createResult->errorString()));
                                return;
                            }
                            const Akonadi::Tag tag = createJob->tag();
                            mTags.insert(tagName, tag);
                            callback(tag, {});
                        });
            });
}

void VimMessageManager::fetchItemsWithTags(const Akonadi::Item::List &items, FetchCallback callback)
{
    const Akonadi::Item::List uniqueItems = uniqueValidItems(items);
    if (uniqueItems.isEmpty()) {
        callback({}, {});
        return;
    }

    auto *fetchJob = new Akonadi::ItemFetchJob(uniqueItems, this);
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

void VimMessageManager::fetchItemsWithTagName(const QString &tagName, FetchCallback callback)
{
    auto *tagFetchJob = new Akonadi::TagFetchJob(this);
    tagFetchJob->fetchScope().setFetchIdOnly(false);
    connect(tagFetchJob,
            &Akonadi::TagFetchJob::result,
            this,
            [this, tagFetchJob, tagName, callback = std::move(callback)](KJob *job) mutable {
                if (job->error()) {
                    callback({}, tr("Não foi possível consultar as tags: %1").arg(job->errorString()));
                    return;
                }

                Akonadi::Tag::List matchingTags;
                for (const Akonadi::Tag &tag : tagFetchJob->tags()) {
                    if (tag.name() == tagName) {
                        matchingTags.push_back(tag);
                    }
                }
                if (matchingTags.isEmpty()) {
                    callback({}, {});
                    return;
                }

                auto pending = std::make_shared<int>(matchingTags.size());
                auto fetchedItems = std::make_shared<Akonadi::Item::List>();
                auto errors = std::make_shared<QStringList>();
                auto completion = std::make_shared<FetchCallback>(std::move(callback));
                for (const Akonadi::Tag &tag : matchingTags) {
                    auto *itemFetchJob = new Akonadi::ItemFetchJob(tag, this);
                    itemFetchJob->fetchScope().setFetchTags(true);
                    itemFetchJob->fetchScope().tagFetchScope().setFetchIdOnly(false);
                    itemFetchJob->fetchScope().setAncestorRetrieval(Akonadi::ItemFetchScope::Parent);
                    connect(itemFetchJob,
                            &Akonadi::ItemFetchJob::result,
                            this,
                            [itemFetchJob, tagName, pending, fetchedItems, errors, completion](KJob *itemResult) {
                                if (itemResult->error()) {
                                    errors->push_back(itemResult->errorString());
                                } else {
                                    fetchedItems->append(itemFetchJob->items());
                                }

                                --*pending;
                                if (*pending != 0) {
                                    return;
                                }
                                if (!errors->isEmpty()) {
                                    (*completion)({},
                                                  QObject::tr("Não foi possível carregar as mensagens da tag '%1': %2")
                                                      .arg(tagName, joinedErrors(*errors)));
                                } else {
                                    (*completion)(uniqueValidItems(*fetchedItems), {});
                                }
                            });
                }
            });
}

void VimMessageManager::setTagState(const Akonadi::Tag &tag,
                                    const Akonadi::Item::List &items,
                                    bool present,
                                    FetchCallback callback)
{
    const Akonadi::Item::List uniqueItems = uniqueValidItems(items);
    Akonadi::Tag::List tagsToClear;
    if (!present) {
        for (const Akonadi::Item &item : uniqueItems) {
            for (const Akonadi::Tag &itemTag : item.tags()) {
                if (itemTag.name() == tag.name() && !tagsToClear.contains(itemTag)) {
                    tagsToClear.push_back(itemTag);
                }
            }
        }
    }

    Akonadi::Item::List changedItems;
    for (const Akonadi::Item &source : uniqueItems) {
        if (hasTagNamed(source, tag.name()) == present) {
            continue;
        }
        Akonadi::Item item(source);
        if (present) {
            item.setTag(tag);
        } else {
            // Apply the same removal set to every item in this batch. Besides
            // clearing accidental homonyms, this is required because a bulk
            // ItemModifyJob derives its tag delta from the first item.
            for (const Akonadi::Tag &itemTag : tagsToClear) {
                item.clearTag(itemTag);
            }
        }
        changedItems.push_back(item);
    }

    if (changedItems.isEmpty()) {
        callback(uniqueItems, {});
        return;
    }

    auto *modifyJob = new Akonadi::ItemModifyJob(changedItems, this);
    modifyJob->setIgnorePayload(true);
    modifyJob->disableRevisionCheck();
    connect(modifyJob,
            &Akonadi::ItemModifyJob::result,
            this,
            [this, changedItems, tag, present, callback = std::move(callback)](KJob *job) mutable {
                if (job->error()) {
                    const QString operation = present ? tr("atribuir") : tr("remover");
                    callback({}, tr("Não foi possível %1 a tag '%2': %3").arg(operation, tag.name(), job->errorString()));
                    return;
                }

                // Verify Akonadi's persisted state before continuing a command.
                fetchItemsWithTags(changedItems,
                                   [tag, present, expectedCount = changedItems.size(), callback = std::move(callback)](
                                       const Akonadi::Item::List &verifiedItems, const QString &fetchError) mutable {
                                       if (!fetchError.isEmpty()) {
                                           callback({}, fetchError);
                                           return;
                                       }
                                       const auto wrongState = std::find_if(
                                           verifiedItems.cbegin(),
                                           verifiedItems.cend(),
                                           [&tag, present](const Akonadi::Item &item) {
                                               return hasTagNamed(item, tag.name()) != present;
                                           });
                                       if (verifiedItems.size() != expectedCount || wrongState != verifiedItems.cend()) {
                                           const QString change = present ? QObject::tr("atribuição") : QObject::tr("remoção");
                                           callback({},
                                                    QObject::tr("O Akonadi não confirmou a %1 da tag '%2'.")
                                                        .arg(change, tag.name()));
                                           return;
                                       }
                                       callback(verifiedItems, {});
                                   });
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

void VimMessageManager::toggleSelectedTag(const Akonadi::Item::List &items)
{
    if (mBusy) {
        return;
    }
    if (items.isEmpty()) {
        Q_EMIT statusMessage(tr("Nenhuma mensagem está selecionada."));
        return;
    }

    setBusy(true);
    resolveTag(selectedTagName, [this, items](const Akonadi::Tag &tag, const QString &tagError) {
        if (!tagError.isEmpty()) {
            finishCommand(tagError);
            return;
        }

        fetchItemsWithTags(items, [this, tag](const Akonadi::Item::List &fetchedItems, const QString &fetchError) {
            if (!fetchError.isEmpty()) {
                finishCommand(fetchError);
                return;
            }

            Akonadi::Item::List addItems;
            Akonadi::Item::List removeItems;
            for (const Akonadi::Item &item : fetchedItems) {
                if (hasTagNamed(item, selectedTagName)) {
                    removeItems.push_back(item);
                } else {
                    addItems.push_back(item);
                }
            }
            if (addItems.isEmpty() && removeItems.isEmpty()) {
                finishCommand(tr("Nenhuma mensagem válida está selecionada."));
                return;
            }

            const int addedCount = addItems.size();
            const int removedCount = removeItems.size();
            setTagState(tag,
                        addItems,
                        true,
                        [this, tag, removeItems, addedCount, removedCount](const Akonadi::Item::List &, const QString &addError) {
                            if (!addError.isEmpty()) {
                                finishCommand(addError);
                                return;
                            }
                            setTagState(tag,
                                        removeItems,
                                        false,
                                        [this, tag, addedCount, removedCount](const Akonadi::Item::List &, const QString &removeError) {
                                            if (!removeError.isEmpty()) {
                                                finishCommand(removeError);
                                                return;
                                            }
                                            registerTagForDisplay(tag);
                                            finishCommand(tr("Seleção atualizada: %1 incluída(s), %2 removida(s).")
                                                              .arg(addedCount)
                                                              .arg(removedCount));
                                        });
                        });
        });
    });
}

void VimMessageManager::assignWorkflowTag(const QString &tagName,
                                          const Akonadi::Item::List &fallbackItems,
                                          const QList<Akonadi::Item::Id> &currentListItemIds)
{
    if (mBusy) {
        return;
    }
    if (fallbackItems.isEmpty() && currentListItemIds.isEmpty()) {
        Q_EMIT statusMessage(tr("Nenhuma mensagem está selecionada."));
        return;
    }

    setBusy(true);
    resolveTag(selectedTagName, [this, tagName, fallbackItems, currentListItemIds](const Akonadi::Tag &selectedTag,
                                                                                 const QString &tagError) {
        if (!tagError.isEmpty()) {
            finishCommand(tagError);
            return;
        }

        // Query every Tag ID with this display name. Besides reusing GENERIC
        // tags, this recovers cleanly from duplicate names left by clients.
        fetchItemsWithTagName(selectedTagName,
                              [this, tagName, fallbackItems, currentListItemIds, selectedTag](
                                  const Akonadi::Item::List &taggedItems, const QString &fetchError) {
                                  if (!fetchError.isEmpty()) {
                                      finishCommand(fetchError);
                                      return;
                                  }

                                  QSet<Akonadi::Item::Id> currentIds;
                                  currentIds.reserve(currentListItemIds.size());
                                  for (const Akonadi::Item::Id id : currentListItemIds) {
                                      if (id > 0) {
                                          currentIds.insert(id);
                                      }
                                  }

                                  Akonadi::Item::List targets;
                                  for (const Akonadi::Item &item : taggedItems) {
                                      if (currentIds.contains(item.id())) {
                                          targets.push_back(item);
                                      }
                                  }

                                  if (!targets.isEmpty()) {
                                      assignWorkflowTagToItems(tagName, selectedTag, targets);
                                  } else {
                                      addFallbackSelection(selectedTag, fallbackItems, tagName);
                                  }
                              });
    });
}

void VimMessageManager::addFallbackSelection(const Akonadi::Tag &selectedTag,
                                             const Akonadi::Item::List &fallbackItems,
                                             const QString &workflowTagName)
{
    if (fallbackItems.isEmpty()) {
        finishCommand(tr("Não há mensagens com a tag 'selected' nesta lista e nenhuma mensagem atual foi encontrada."));
        return;
    }

    fetchItemsWithTags(fallbackItems,
                       [this, selectedTag, workflowTagName](const Akonadi::Item::List &fetchedItems, const QString &fetchError) {
                           if (!fetchError.isEmpty()) {
                               finishCommand(fetchError);
                               return;
                           }
                           if (fetchedItems.isEmpty()) {
                               finishCommand(tr("Nenhuma mensagem válida está selecionada."));
                               return;
                           }

                           setTagState(selectedTag,
                                       fetchedItems,
                                       true,
                                       [this, selectedTag, workflowTagName, fetchedItems](const Akonadi::Item::List &,
                                                                                        const QString &selectError) {
                                           if (!selectError.isEmpty()) {
                                               finishCommand(selectError);
                                               return;
                                           }
                                           registerTagForDisplay(selectedTag);
                                           assignWorkflowTagToItems(workflowTagName, selectedTag, fetchedItems);
                                       });
                       });
}

void VimMessageManager::assignWorkflowTagToItems(const QString &tagName,
                                                 const Akonadi::Tag &selectedTag,
                                                 const Akonadi::Item::List &items)
{
    resolveTag(tagName, [this, tagName, selectedTag, items](const Akonadi::Tag &tag, const QString &tagError) {
        if (!tagError.isEmpty()) {
            finishCommand(tagError);
            return;
        }

        fetchItemsWithTags(items, [this, tagName, selectedTag, tag](const Akonadi::Item::List &fetchedItems, const QString &fetchError) {
            if (!fetchError.isEmpty()) {
                finishCommand(fetchError);
                return;
            }

            Akonadi::Item::List changedItems;
            for (const Akonadi::Item &item : fetchedItems) {
                if (!hasTagNamed(item, tagName)) {
                    changedItems.push_back(item);
                }
            }

            if (changedItems.isEmpty()) {
                registerTagForDisplay(tag);
                clearSelectionAfterAssignment(tagName, selectedTag, fetchedItems, 0, true);
                return;
            }

            setTagState(tag,
                        changedItems,
                        true,
                        [this, tagName, selectedTag, tag, fetchedItems](const Akonadi::Item::List &verifiedItems,
                                                                      const QString &modifyError) {
                            if (!modifyError.isEmpty()) {
                                finishCommand(modifyError);
                                return;
                            }

                            mUndoTag = tag;
                            mUndoItems = verifiedItems;
                            registerTagForDisplay(tag);
                            Q_EMIT stateChanged();
                            clearSelectionAfterAssignment(tagName, selectedTag, fetchedItems, verifiedItems.size(), false);
                        });
        });
    });
}

void VimMessageManager::clearSelectionAfterAssignment(const QString &tagName,
                                                      const Akonadi::Tag &selectedTag,
                                                      const Akonadi::Item::List &items,
                                                      int assignedCount,
                                                      bool alreadyTagged)
{
    fetchItemsWithTags(items, [this, tagName, selectedTag, assignedCount, alreadyTagged](const Akonadi::Item::List &fetchedItems,
                                                                                       const QString &fetchError) {
        if (!fetchError.isEmpty()) {
            finishCommand(tr("A tag '%1' foi processada, mas a seleção não pôde ser limpa: %2").arg(tagName, fetchError));
            return;
        }

        Akonadi::Item::List selectedItems;
        for (const Akonadi::Item &item : fetchedItems) {
            if (hasTagNamed(item, selectedTagName)) {
                selectedItems.push_back(item);
            }
        }

        setTagState(selectedTag,
                    selectedItems,
                    false,
                    [this, tagName, assignedCount, alreadyTagged](const Akonadi::Item::List &, const QString &clearError) {
                        if (!clearError.isEmpty()) {
                            finishCommand(tr("A tag '%1' foi processada, mas a seleção não pôde ser limpa: %2")
                                              .arg(tagName, clearError));
                            return;
                        }
                        if (alreadyTagged) {
                            finishCommand(tr("As mensagens já possuíam a tag '%1'; a seleção foi limpa.").arg(tagName));
                        } else {
                            finishCommand(tr("Tag '%1' atribuída a %2 mensagem(ns); seleção limpa.")
                                              .arg(tagName)
                                              .arg(assignedCount));
                        }
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
    if (mBusy) {
        return;
    }
    if (items.isEmpty()) {
        Q_EMIT statusMessage(tr("Nenhuma mensagem está selecionada."));
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
    Akonadi::Tag::List tagsToClear;
    for (const Akonadi::Item &item : items) {
        for (const Akonadi::Tag &tag : item.tags()) {
            if (isWorkflowTag(tag) && !tagsToClear.contains(tag)) {
                tagsToClear.push_back(tag);
            }
        }
    }
    if (tagsToClear.isEmpty()) {
        callback({});
        return;
    }

    Akonadi::Item::List changedItems = items;
    for (Akonadi::Item &item : changedItems) {
        // Bulk ItemModifyJob uses the first item's change log for every item,
        // so every clone must carry the same tag-removal delta.
        for (const Akonadi::Tag &tag : tagsToClear) {
            item.clearTag(tag);
        }
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
