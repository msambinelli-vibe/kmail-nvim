/*
    SPDX-FileCopyrightText: 2026 KMail Vim Navigation contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <PimCommon/GenericPlugin>
#include <PimCommonAkonadi/GenericPluginInterface>

#include <QVariant>

class VimNavigationPlugin final : public PimCommon::GenericPlugin
{
    Q_OBJECT

public:
    explicit VimNavigationPlugin(QObject *parent = nullptr, const QList<QVariant> &arguments = {});
    ~VimNavigationPlugin() override;

    [[nodiscard]] PimCommon::GenericPluginInterface *createInterface(QObject *parent = nullptr) override;
};
