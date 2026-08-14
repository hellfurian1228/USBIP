#ifndef AUDIORELAYMANAGER_H
#define AUDIORELAYMANAGER_H

#include <QObject>
#include <QAudioSource>
#include <QAudioSink>
#include <QMediaDevices>
#include <QUdpSocket>
#include <QAudioFormat>
#include <QAudioDevice>
#include <QHostAddress>
#include <QStringList>

class AudioRelayManager : public QObject {
    Q_OBJECT
public:
    explicit AudioRelayManager(QObject *parent = nullptr);
    ~AudioRelayManager();

    QStringList getAvailableInputDevices();
    QStringList getAvailableOutputDevices();

    void startStreaming(const QAudioDevice& device, const QHostAddress& targetIp, quint16 targetPort, int sampleRate, int channelCount);
    void startReceiving(const QAudioDevice& device, quint16 listenPort);
    void stopAll();

    // Helper to get QAudioDevice by name
    QAudioDevice findInputDevice(const QString& name);
    QAudioDevice findOutputDevice(const QString& name);

private:
    QUdpSocket* udpSocket;
    QAudioSource* audioInput;
    QAudioSink* audioOutput;
    QIODevice* inputDevice;
    QIODevice* outputDevice;
};

#endif // AUDIORELAYMANAGER_H
