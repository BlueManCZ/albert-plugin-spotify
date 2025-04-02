// Copyright (c) 2020-2025 Ivo Šmerek

#pragma once
#include "spotifyApiClient.h"
#include <albert/extensionplugin.h>
#include <albert/triggerqueryhandler.h>
#include <memory>
class SpotifyApiClient;


class Plugin final : public albert::ExtensionPlugin,
                     public albert::TriggerQueryHandler
{
    ALBERT_PLUGIN

public:
    Plugin();
    ~Plugin() override;

private:
    std::unique_ptr<SpotifyApiClient> api;

    QString defaultTrigger() const override;
    void handleTriggerQuery(albert::Query&) override;
    QWidget* buildConfigWidget() override;

    QString settingsString(QAnyStringView key, const QVariant& defaultValue = {}) const;
    int settingsInt(QAnyStringView key, const QVariant& defaultValue = {}) const;
    bool settingsBool(QAnyStringView key, const QVariant& defaultValue = {}) const;
};
