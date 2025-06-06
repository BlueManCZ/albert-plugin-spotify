// Copyright (C) 2020-2025 Ivo Šmerek

#pragma once
#include <QString>

class Track
{
public:
    QString id;
    QString name;
    QString artists;
    QString albumId;
    QString albumName;
    QString uri;
    QString imageUrl;
    bool isExplicit = false;
};
