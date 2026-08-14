/*
    SPDX-FileCopyrightText: 2026 KMail Vim Navigation contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "vimnavigationplugin.h"

#include "vimnavigationplugininterface.h"

#include <KPluginFactory>

K_PLUGIN_CLASS_WITH_JSON(VimNavigationPlugin, "kmail_vimnavigationplugin.json")

VimNavigationPlugin::VimNavigationPlugin(QObject *parent, const QList<QVariant> &)
    : PimCommon::GenericPlugin(parent)
{
}

VimNavigationPlugin::~VimNavigationPlugin() = default;

PimCommon::GenericPluginInterface *VimNavigationPlugin::createInterface(QObject *parent)
{
    return new VimNavigationPluginInterface(parent);
}

#include "vimnavigationplugin.moc"
