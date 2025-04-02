// Copyright (c) 2020-2025 Ivo Šmerek

#include "plugin.h"
#include "spotifyApiClient.h"
#include "ui_configwidget.h"
#include <QDir>
#include <QMessageBox>
#include <QSettings>
#include <QThread>
#include <albert/albert.h>
#include <albert/logging.h>
#include <albert/standarditem.h>
ALBERT_LOGGING_CATEGORY("spotify")
using namespace albert;
using namespace std;


inline auto CFG_CLIENT_ID = "client_id";
inline auto CFG_CLIENT_SECRET = "client_secret";
inline auto CFG_REFRESH_TOKEN = "refresh_token";
inline auto CFG_ALLOW_EXPLICIT = "allow_explicit";
inline auto DEF_ALLOW_EXPLICIT = true;
inline auto CFG_NUM_RESULTS = "number_of_results";
inline auto DEF_NUM_RESULTS = 5;
inline auto CFG_SPOTIFY_EXECUTABLE = "spotify_executable";
inline auto DEF_SPOTIFY_EXECUTABLE = "spotify";
inline auto STATE_LAST_DEVICE = "last_device";
inline auto COVERS_DIR_NAME = "covers";


Plugin::Plugin()
{
    const auto s = settings();

    api = make_unique<SpotifyApiClient>(
        s->value(CFG_CLIENT_ID).toString(),
        s->value(CFG_CLIENT_SECRET).toString(),
        s->value(CFG_REFRESH_TOKEN).toString()
    );

    fetch_count_ = s->value(CFG_NUM_RESULTS).toUInt();
    show_explicit_content_ = s->value(CFG_ALLOW_EXPLICIT).toBool();
    spotify_command_ = s->value(CFG_SPOTIFY_EXECUTABLE).toString();
}

Plugin::~Plugin() = default;

QString Plugin::defaultTrigger() const { return "play "; }

void Plugin::handleTriggerQuery(Query &query)
{
    if (const auto trimmed = query.string().trimmed(); trimmed.isEmpty())
        return;

    if (!query.isValid())
        return;

    // If there is no internet connection, make one alerting item to let the user know.
    if (!api->checkServerResponse())
    {
        DEBG << "No internet connection!";
        query.add(StandardItem::make(nullptr, "Can't get an answer from the server.",
                                      "Please, check your internet connection.", nullptr));
        return;
    }

    // If the access token expires, try to refresh it or alert the user what is wrong.
    if (api->isAccessTokenExpired())
    {
        DEBG << "Token expired. Refreshing";
        if (!api->refreshAccessToken())
        {
            query.add(StandardItem::make(nullptr, "Wrong credentials.",
                                          "Please, check the extension settings.", nullptr));
            return;
        }
    }

    // Search for tracks on Spotify using the query.
    const auto tracks = api->searchTracks(query.string(), fetchCount());

    const auto coversCacheLocation = cacheLocation() / COVERS_DIR_NAME;

    if (!is_directory(coversCacheLocation))
        tryCreateDirectory(coversCacheLocation);

    for (const auto& track : tracks)
    {
        // If the track is explicit and the user doesn't want to see explicit tracks, skip it.
        if (track.isExplicit && !showExplicitContent())
            continue;

        const auto filename = QString("%1/%2.jpeg").arg(coversCacheLocation.c_str(), track.albumId);

        // Download cover image of the album.
        api->downloadFile(track.imageUrl, filename);

        // Create a standard item with a track name in title and album with artists in subtext.
        const auto result = StandardItem::make(
            track.id,
            track.name,
            QString("%1 (%2)").arg(track.albumName, track.artists),
            nullptr,
            {filename});

        auto actions = vector<Action>();

        actions.emplace_back(
            "play",
            "Play on Spotify",
            [this, track]
            {
                // If we have no devices run local Spotify client
                if (const auto devices = api->getDevices();
                    devices.isEmpty())
                {
                    runDetachedProcess({spotify_command_});
                    api->waitForDeviceAndPlay(track);
                    INFO << "Playing on local Spotify.";
                }

                // If available, use an active device and play the track.
                else if (auto it = ranges::find_if(devices, &Device::isActive);
                    it != devices.cend())
                {
                    const auto activeDevice = *it;
                    api->playTrack(track, it->id);
                    INFO << "Playing on active device:" << it->name;
                    state()->setValue(STATE_LAST_DEVICE, it->id);
                }

                // If available, use the last-used device.
                else if (it = ranges::find_if(devices,
                            [id=state()->value(STATE_LAST_DEVICE).toString()](const auto &d)
                            { return d.id == id; });
                         it != devices.end())
                {
                    api->playTrack(track, it->id);
                    INFO << "Playing on last used device:" << it->name;
                }

                // Otherwise Use the first available device.
                else
                {
                    api->playTrack(track, devices[0].id);
                    INFO << "Playing on:" << devices[0].id;
                    state()->setValue(STATE_LAST_DEVICE, devices[0].id);
                }
            }
        );

        actions.emplace_back("queue", "Add to the Spotify queue",
                             [this, track] { api->addTrackToQueue(track); });

        // For each device except active create action to transfer Spotify playback to this device.
        for (const auto& device : api->getDevices())
        {
            if (device.isActive) continue;

            actions.emplace_back(
                QString("play_on_%1").arg(device.id),
                QString("Play on %1 (%2)").arg(device.type, device.name),
                [this, track, device]
                {
                    api->playTrack(track, device.id);
                    state()->setValue(STATE_LAST_DEVICE, device.id);
                }
            );
        }

        result->setActions(actions);

        query.add(result);
    }
}

QWidget* Plugin::buildConfigWidget()
{
    auto* widget = new QWidget();
    Ui::ConfigWidget ui;
    ui.setupUi(widget);

    ui.lineEdit_client_id->setText(clientId());
    connect(ui.lineEdit_client_id, &QLineEdit::textEdited,
            this, &Plugin::setClientId);

    ui.lineEdit_client_secret->setText(clientSecret());
    connect(ui.lineEdit_client_secret, &QLineEdit::textEdited,
            this, &Plugin::setClientSecret);

    ui.lineEdit_refresh_token->setText(refreshToken());
    connect(ui.lineEdit_refresh_token, &QLineEdit::textEdited,
            this, &Plugin::setRefreshToken);

    ui.checkBox_explicit->setChecked(showExplicitContent());
    connect(ui.checkBox_explicit, &QCheckBox::toggled,
            this, &Plugin::setShowExplicitContent);

    ui.spinBox_number_of_results->setValue(fetchCount());
    connect(ui.spinBox_number_of_results, &QSpinBox::valueChanged,
            this, &Plugin::setFetchCount);

    ui.lineEdit_spotify_executable->setText(spotifyCommand());
    connect(ui.lineEdit_spotify_executable, &QLineEdit::textEdited,
            this, &Plugin::setSpotifyCommand);

    // Bind "Test connection" button
    connect(ui.pushButton_test_connection, &QPushButton::clicked, this, [this]
    {
        const bool refreshStatus = api->refreshAccessToken();

        QString message = "Everything is set up correctly.";
        if (!refreshStatus)
        {
            message = api->lastErrorMessage.isEmpty()
                          ? "Can't get an answer from the server.\nPlease, check your internet connection."
                          : QString("Spotify Web API returns: \"%1\"\nPlease, check all input fields.")
                          .arg(api->lastErrorMessage);
        }

        const auto messageBox = new QMessageBox();
        messageBox->setWindowTitle(refreshStatus ? "Success" : "API error");
        messageBox->setText(message);
        messageBox->setIcon(refreshStatus ? QMessageBox::Information : QMessageBox::Critical);
        messageBox->exec();
        delete messageBox;
    });

    return widget;
}

QString Plugin::clientId() const { return api->clientId(); }

void Plugin::setClientId(const QString &v)
{
    if(api->clientId() == v)
        return;

    api->setClientId(v);
    settings()->setValue(CFG_CLIENT_ID, v);
}

QString Plugin::clientSecret() const { return api->clientSecret(); }

void Plugin::setClientSecret(const QString &v)
{
    if(api->clientSecret() == v)
        return;

    api->setClientSecret(v);
    settings()->setValue(CFG_CLIENT_SECRET, v);
}

QString Plugin::refreshToken() const { return api->refreshToken(); }

void Plugin::setRefreshToken(const QString &v)
{
    if(api->refreshToken() == v)
        return;

    api->setRefreshToken(v);
    settings()->setValue(CFG_REFRESH_TOKEN, v);
}

uint Plugin::fetchCount() const { return fetch_count_; }

void Plugin::setFetchCount(uint v)
{
    if(fetch_count_ == v)
        return;

    fetch_count_ = v;
    settings()->setValue(CFG_NUM_RESULTS, v);
}

bool Plugin::showExplicitContent() const { return show_explicit_content_; }

void Plugin::setShowExplicitContent(bool v)
{
    if(show_explicit_content_ == v)
        return;

    show_explicit_content_ = v;
    settings()->setValue(CFG_ALLOW_EXPLICIT, v);
}

QString Plugin::spotifyCommand() const { return spotify_command_; }

void Plugin::setSpotifyCommand(const QString &v)
{
    if(spotify_command_ == v)
        return;

    spotify_command_ = v;
    if (v.isEmpty())
        settings()->remove(CFG_SPOTIFY_EXECUTABLE);
    else
        settings()->setValue(CFG_SPOTIFY_EXECUTABLE, v);
}
