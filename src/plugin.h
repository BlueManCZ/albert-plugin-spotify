// Copyright (c) 2020-2025 Ivo Šmerek

#pragma once
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

    QString defaultTrigger() const override;
    void handleTriggerQuery(albert::Query&) override;
    QWidget* buildConfigWidget() override;

    QString clientId() const;
    void setClientId(const QString &);

    QString clientSecret() const;
    void setClientSecret(const QString &);

    QString refreshToken() const;
    void setRefreshToken(const QString &);

    uint fetchCount() const;
    void setFetchCount(uint);

    bool showExplicitContent() const;
    void setShowExplicitContent(bool);

    QString spotifyCommand() const;
    void setSpotifyCommand(const QString &);

private:

    std::unique_ptr<SpotifyApiClient> api;

    uint fetch_count_;
    bool show_explicit_content_;
    QString spotify_command_;

};
