#ifndef AUDIORELAYMANAGER_H
#define AUDIORELAYMANAGER_H

#include <QObject>
#include <QUdpSocket>
#include <QHostAddress>
#include <QStringList>

#ifdef HAVE_QT_MULTIMEDIA
#include <QAudioSource>
#include <QAudioSink>
#include <QMediaDevices>
#include <QAudioFormat>
#include <QAudioDevice>
#endif

#include <opus.h>
#include <atomic>

extern std::atomic<uint64_t> g_totalBytesTransferred;

class AudioRelayManager : public QObject {
    Q_OBJECT
public:
    explicit AudioRelayManager(QObject *parent = nullptr);
    ~AudioRelayManager();

    QStringList getAvailableInputDevices();
    QStringList getAvailableOutputDevices();

#ifdef HAVE_QT_MULTIMEDIA
    void startStreaming(const QAudioDevice& device, const QHostAddress& targetIp, quint16 targetPort, int sampleRate, int channelCount, int bitrate);
    void startReceiving(const QAudioDevice& device, quint16 listenPort);
#endif
    void stopAll();

#ifdef HAVE_QT_MULTIMEDIA
    // Helper to get QAudioDevice by name
    QAudioDevice findInputDevice(const QString& name);
    QAudioDevice findOutputDevice(const QString& name);
#endif

private:
    QUdpSocket* udpSocket;
#ifdef HAVE_QT_MULTIMEDIA
    QAudioSource* audioInput;
    QAudioSink* audioOutput;
#endif
    QIODevice* inputDevice;
    QIODevice* outputDevice;
    OpusEncoder* opusEncoder = nullptr;
    OpusDecoder* opusDecoder = nullptr;
    QByteArray pcmAccumulator;
    uint64_t byteCounter;
};

#endif // AUDIORELAYMANAGER_H
