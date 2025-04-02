// Copyright (c) 2020-2025 Ivo Šmerek

#pragma once
#include "types/device.h"
#include "types/track.h"
#include <QDateTime>
#include <QObject>
#include <QReadWritelock>
class QNetworkRequest;



/**
 * Spotify API client for interacting with the Spotify Web API.
 */
class SpotifyApiClient final : public QObject
{
public:
    explicit SpotifyApiClient(QString clientId, QString clientSecret, QString refreshToken);

    /** Contains string description of the last error message. */
    QString lastErrorMessage;

    QString clientId();
    void setClientId(const QString& id);

    QString clientSecret();
    void setClientSecret(const QString& secret);

    QString refreshToken();
    void setRefreshToken(const QString& token);

    /**
     * Check if the access token is expired.
     * @return true if the access token is expired, false otherwise.
     */
    bool isAccessTokenExpired() const;

    // WEB API CALLS //

    /**
     * Request and store a new access token from Spotify.
     * @return true if the accessToken was successfully refreshed.
     */
    bool refreshAccessToken();

    /**
     * Check response of Spotify API server.
     * @return true if server returns any response, false otherwise.
     */
    bool checkServerResponse() const;

    /**
     * Download a file from the given URL and save it to the given file path.
     * It will not download the file if the given pilePath already exists.
     * @param url URL to download.
     * @param filePath File path to save the file to.
     */
    void downloadFile(const QString& url, const QString& filePath);

    /**
     * Search for tracks on Spotify.
     * @param query The search query.
     * @param limit The maximum number of tracks to return.
     * @return A list of tracks found by the search.
     */
    QVector<Track> searchTracks(const QString& query, int limit);

    /**
     * Returns list of users available Spotify devices.
     */
    QVector<Device> getDevices();

    /**
     * Wait for any device to be ready.
     * @param track
     */
    void waitForDevice(const Track& track);

    /**
     * Wait for any device to be ready and play a track on it.
     * @param track The track object to play.
     */
    void waitForDeviceAndPlay(const Track& track);

    /**
     * Add a track to the queue of a specific device.
     * @param track The track object to add to the queue.
     */
    void addTrackToQueue(const Track& track) const;

   public slots:
    /**
     * Play a track on a specific device.
     * @param track The track object to play.
     * @param deviceId The ID of the device to play the track on.
     */
    void playTrack(const Track& track, const QString& deviceId) const;

private:
    Q_OBJECT

    QString clientId_;
    QString clientSecret_;
    QString refreshToken_;
    QString accessToken;

    QDateTime expirationTime;
    QReadWriteLock fileLock;

    /**
     * Wait for a specific signal from an object.
     * @param sender The object emitting the signal.
     * @param signal The signal to wait for.
     */
    static void waitForSignal(const QObject* sender, const char* signal);

    /**
     * Convert a JSON string to a JSON object.
     * @param string The JSON string to convert.
     * @return The JSON object.
     */
    static QJsonObject stringToJson(const QString& string);

    /**
     * Create a network request with the given URL.
     * @param url The URL to create the request for.
     * @return The created request with access token from instance.
     */
    QNetworkRequest createRequest(const QUrl& url) const;

    /**
     * Parse a JSON object to a device object.
     * @param deviceData The JSON object to parse.
     * @return The parsed device object.
     */
    static Device parseDevice(QJsonObject deviceData);

    /**
     * Parse a JSON object to a track object.
     * @param trackData The JSON object to parse.
     * @return The parsed track object.
     */
    static Track parseTrack(QJsonObject trackData);

    /**
     * Linearize a list of artists to a single string.
     * @param artists The list of artists to linearize.
     * @return String of artists separated by commas.
     */
    static QString linearizeArtists(const QJsonArray& artists);

signals:
    void deviceReady(const Track&, QString);
};
