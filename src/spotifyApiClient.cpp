// Copyright (c) 2020-2025 Ivo Šmerek

#include "spotifyApiClient.h"
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QSaveFile>
#include <QtConcurrentRun>
#include <albert/albert.h>
#include <albert/logging.h>
using namespace albert;
using namespace std;

inline QString TOKEN_URL = "https://accounts.spotify.com/api/token";
inline QString SEARCH_URL = "https://api.spotify.com/v1/search?q=%1&type=%2&limit=%3";
inline QString DEVICES_URL = "https://api.spotify.com/v1/me/player/devices";
inline QString QUEUE_URL = "https://api.spotify.com/v1/me/player/queue?uri=%1";
inline QString PLAY_URL = "https://api.spotify.com/v1/me/player/play?device_id=%1";
inline int DEFAULT_TIMEOUT = 10000;


SpotifyApiClient::SpotifyApiClient(QString id, QString secret, QString token):
    clientId_(id),
    clientSecret_(secret),
    refreshToken_(token)
{
}

QString SpotifyApiClient::clientId() { return clientId_; }

void SpotifyApiClient::setClientId(const QString &id) { clientId_ = id; }

QString SpotifyApiClient::clientSecret() { return clientSecret_; }

void SpotifyApiClient::setClientSecret(const QString &secret) { clientSecret_ = secret; }

QString SpotifyApiClient::refreshToken() { return refreshToken_; }

void SpotifyApiClient::setRefreshToken(const QString &token) { refreshToken_ = token; }

bool SpotifyApiClient::isAccessTokenExpired() const
{
    return QDateTime::currentDateTime() > expirationTime;
}

bool SpotifyApiClient::refreshAccessToken()
{
    auto request = QNetworkRequest(QUrl(TOKEN_URL));

    const auto hash = QString("%1:%2").arg(clientId_, clientSecret_).toUtf8().toBase64();
    const auto header = QString("Basic ").append(hash);

    request.setRawHeader(QByteArray("Authorization"), header.toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(QString("application/x-www-form-urlencoded")));
    request.setTransferTimeout(DEFAULT_TIMEOUT);

    const auto savedToken = accessToken;
    const auto postData = QString("grant_type=refresh_token&refresh_token=%1").arg(refreshToken_).toLocal8Bit();
    const auto reply = network().post(request, postData);

    connect(reply, &QNetworkReply::finished, this, [this, reply]
    {
        reply->deleteLater();

        const auto replyString = reply->readAll();

        if (const auto jsonVariant = stringToJson(replyString); !jsonVariant["access_token"].isUndefined())
        {
            accessToken = jsonVariant["access_token"].toString();
            expirationTime = QDateTime::currentDateTime().addSecs(jsonVariant["expires_in"].toInt());
            lastErrorMessage = "";
        }
        else
        {
            accessToken = "";
            lastErrorMessage = jsonVariant[
                !jsonVariant["error_description"].isUndefined() ? "error_description" : "error"
            ].toString();
        }
    });

    waitForSignal(reply, SIGNAL(finished()));

    return !accessToken.isEmpty() && savedToken != accessToken;
}

bool SpotifyApiClient::checkServerResponse() const
{
    try
    {
        const auto request = QNetworkRequest(QUrl(TOKEN_URL));
        const auto reply = network().get(request);

        waitForSignal(reply, SIGNAL(finished()));

        reply->deleteLater();

        return reply->bytesAvailable();
    }
    catch (...)
    {
        return false;
    }
}

void SpotifyApiClient::downloadFile(const QString& url, const QString& filePath)
{
    if (const QFileInfo fileInfo(filePath); fileInfo.exists())
        return;

    fileLock.lockForWrite();

    const auto request = QNetworkRequest(url);
    const auto reply = network().get(request);

    connect(reply, &QNetworkReply::finished, this, [reply, filePath]
    {
        reply->deleteLater();

        if (reply->bytesAvailable())
        {
            QSaveFile file(filePath);
            file.open(QIODevice::WriteOnly);
            file.write(reply->readAll());
            file.commit();
        }
    });

    waitForSignal(reply, SIGNAL(finished()));

    fileLock.unlock();
}

QVector<Track> SpotifyApiClient::searchTracks(const QString& query, const int limit)
{
    const auto url = QUrl(SEARCH_URL.arg(query, "track", QString::number(limit)));
    const auto request = createRequest(url);
    const auto reply = network().get(request);

    auto tracksArray = make_shared<QJsonArray>();

    connect(reply, &QNetworkReply::finished, [reply, tracksArray]
    {
        reply->deleteLater();

        const auto jsonObject = stringToJson(reply->readAll());

        *tracksArray = jsonObject["tracks"].toObject()["items"].toArray();
    });

    waitForSignal(reply, SIGNAL(finished()));

    const auto tracks = make_shared<QVector<Track>>();

    for (const auto &trackData : as_const(*tracksArray))
    {
        tracks->append(parseTrack(trackData.toObject()));
    }

    return *tracks;
}

QVector<Device> SpotifyApiClient::getDevices()
{
    const auto request = createRequest(QUrl(DEVICES_URL));
    const auto reply = network().get(request);

    auto devicesArray = make_shared<QJsonArray>();

    connect(reply, &QNetworkReply::finished, [reply, devicesArray]
    {
        reply->deleteLater();

        QJsonObject jsonObject = stringToJson(reply->readAll());

        *devicesArray = jsonObject["devices"].toArray();
    });

    waitForSignal(reply, SIGNAL(finished()));

    const auto devices = make_shared<QVector<Device>>();

    for (auto deviceData : as_const(*devicesArray))
    {
        devices->append(parseDevice(deviceData.toObject()));
    }

    return *devices;
}

void SpotifyApiClient::waitForDevice(const Track& track)
{
    const auto request = createRequest(QUrl(DEVICES_URL));
    const auto reply = network().get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, track]
    {
        reply->deleteLater();

        const auto jsonObject = stringToJson(reply->readAll());
        const auto devicesResult = jsonObject["devices"].toArray();

        if (devicesResult.isEmpty())
        {
            waitForDevice(track);
            return;
        }

        emit deviceReady(track, devicesResult.at(0).toObject()["id"].toString());
    });
}

void SpotifyApiClient::waitForDeviceAndPlay(const Track& track)
{
    connect(this, &SpotifyApiClient::deviceReady, this, &SpotifyApiClient::playTrack);
    waitForDevice(track);
}

void SpotifyApiClient::addTrackToQueue(const Track& track) const
{
    const auto request = createRequest(QUrl(QUEUE_URL.arg(track.uri)));
    network().post(request, "");
}

void SpotifyApiClient::playTrack(const Track& track, const QString& deviceId) const
{
    const auto request = createRequest(QUrl(PLAY_URL.arg(deviceId)));
    const auto postData = QString(R"({"uris": ["%1"]})").arg(track.uri).toLocal8Bit();
    network().put(request, postData);
}

// PRIVATE METHODS

void SpotifyApiClient::waitForSignal(const QObject* sender, const char* signal)
{
    QEventLoop loop;
    connect(sender, signal, &loop, SLOT(quit()));
    loop.exec();
}

QJsonObject SpotifyApiClient::stringToJson(const QString& string)
{
    return QJsonDocument::fromJson(string.toUtf8()).object();
}

QNetworkRequest SpotifyApiClient::createRequest(const QUrl& url) const
{
    const auto request = make_shared<QNetworkRequest>(url);
    const auto header = QString("Bearer ") + accessToken;

    request->setRawHeader(QByteArray("Authorization"), header.toUtf8());
    request->setRawHeader(QByteArray("Accept"), "application/json");
    request->setHeader(QNetworkRequest::ContentTypeHeader, QVariant(QString("application/json")));
    request->setTransferTimeout(DEFAULT_TIMEOUT);

    return *request;
}

Device SpotifyApiClient::parseDevice(QJsonObject deviceData)
{
    auto device = Device();

    device.id = deviceData["id"].toString();
    device.name = deviceData["name"].toString();
    device.type = deviceData["type"].toString();
    device.isActive = deviceData["is_active"].toBool();

    return device;
}

Track SpotifyApiClient::parseTrack(QJsonObject trackData)
{
    auto track = Track();

    track.id = trackData["id"].toString();
    track.name = trackData["name"].toString();
    track.artists = linearizeArtists(trackData["artists"].toArray());
    track.albumId = trackData["album"].toObject()["id"].toString();
    track.albumName = trackData["album"].toObject()["name"].toString();
    track.uri = trackData["uri"].toString();
    track.imageUrl = trackData["album"].toObject()["images"].toArray()[2].toObject()["url"].toString();
    track.isExplicit = trackData["explicit"].toBool();

    return track;
}

QString SpotifyApiClient::linearizeArtists(const QJsonArray& artists)
{
    QStringList linearizedArtists;
    ranges::transform(artists, back_inserter(linearizedArtists),
                      [](const QJsonValue& artist){ return artist["name"].toString(); });

    return linearizedArtists.join(", ");
}
