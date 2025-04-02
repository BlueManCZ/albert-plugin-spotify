cmake_minimum_required(VERSION 3.16)
project(spotify VERSION 1.1)

find_package(Albert REQUIRED)

albert_plugin(QT Widgets Concurrent Network)
