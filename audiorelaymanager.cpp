#include "audiorelaymanager.h"

#include <opus.h>

AudioRelayManager::AudioRelayManager(QObject *parent)
    : QObject(parent),
      udpSocket(nullptr),
#ifdef HAVE_QT_MULTIMEDIA
      audioInput(nullptr),
      audioOutput(nullptr),
#endif
      inputDevice(nullptr),
      outputDevice(nullptr) {
    udpSocket = new QUdpSocket(this);
}

AudioRelayManager::~AudioRelayManager() {
    stopAll();
}

QStringList AudioRelayManager::getAvailableInputDevices() {
    QStringList list;
#ifdef HAVE_QT_MULTIMEDIA
    for (const QAudioDevice &device : QMediaDevices::audioInputs()) {
        list << device.description();
    }
#endif
    return list;
}

QStringList AudioRelayManager::getAvailableOutputDevices() {
    QStringList list;
#ifdef HAVE_QT_MULTIMEDIA
    for (const QAudioDevice &device : QMediaDevices::audioOutputs()) {
        list << device.description();
    }
#endif
    return list;
}

#ifdef HAVE_QT_MULTIMEDIA
QAudioDevice AudioRelayManager::findInputDevice(const QString& name) {
    for (const QAudioDevice &device : QMediaDevices::audioInputs()) {
        if (device.description() == name) {
            return device;
        }
    }
    return QMediaDevices::defaultAudioInput();
}

QAudioDevice AudioRelayManager::findOutputDevice(const QString& name) {
    for (const QAudioDevice &device : QMediaDevices::audioOutputs()) {
        if (device.description() == name) {
            return device;
        }
    }
    return QMediaDevices::defaultAudioOutput();
}

void AudioRelayManager::startStreaming(const QAudioDevice& device, const QHostAddress& targetIp, quint16 targetPort, int sampleRate, int channelCount, int bitrate) {
    if (audioInput) {
        audioInput->stop();
        delete audioInput;
        audioInput = nullptr;
    }
    inputDevice = nullptr;
    pcmAccumulator.clear();

    if (opusEncoder) {
        opus_encoder_destroy(opusEncoder);
        opusEncoder = nullptr;
    }
    int err;
    opusEncoder = opus_encoder_create(sampleRate, channelCount, OPUS_APPLICATION_AUDIO, &err);
    if (opusEncoder)
        opus_encoder_ctl(opusEncoder, OPUS_SET_BITRATE(bitrate));

    QAudioFormat format;
    format.setSampleRate(sampleRate);
    format.setChannelCount(channelCount);
    format.setSampleFormat(QAudioFormat::Int16);

    audioInput = new QAudioSource(device, format, this);
    inputDevice = audioInput->start();

    if (inputDevice) {
        connect(inputDevice, &QIODevice::readyRead, this, [this, targetIp, targetPort]() {
            if (!inputDevice || !udpSocket)
                return;
            pcmAccumulator.append(inputDevice->readAll());
            while (pcmAccumulator.size() >= 3840) {
                QByteArray pcmChunk = pcmAccumulator.left(3840);
                unsigned char outPacket[4000];
                opus_int32 encoded = opus_encode(
                    opusEncoder,
                    reinterpret_cast<const opus_int16*>(pcmChunk.constData()),
                    960, outPacket, sizeof(outPacket));
                if (encoded > 0)
                    udpSocket->writeDatagram(
                        reinterpret_cast<const char*>(outPacket), encoded,
                        targetIp, targetPort);
                pcmAccumulator.remove(0, 3840);
            }
        });
    }
}

void AudioRelayManager::startReceiving(const QAudioDevice& device, quint16 listenPort) {
    if (audioOutput) {
        audioOutput->stop();
        delete audioOutput;
        audioOutput = nullptr;
    }
    outputDevice = nullptr;

    if (opusDecoder) {
        opus_decoder_destroy(opusDecoder);
        opusDecoder = nullptr;
    }
    int err;
    opusDecoder = opus_decoder_create(48000, 2, &err);

    QAudioFormat format;
    format.setSampleRate(48000);
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::Int16);

    audioOutput = new QAudioSink(device, format, this);
    outputDevice = audioOutput->start();

    if (udpSocket) {
        udpSocket->close();
        disconnect(udpSocket, &QUdpSocket::readyRead, this, nullptr);
        udpSocket->bind(QHostAddress::AnyIPv4, listenPort);
        connect(udpSocket, &QUdpSocket::readyRead, this, [this]() {
            while (udpSocket && udpSocket->hasPendingDatagrams()) {
                QByteArray datagram;
                datagram.resize(udpSocket->pendingDatagramSize());
                QHostAddress sender;
                quint16 senderPort;
                udpSocket->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);
                if (outputDevice && outputDevice->isOpen() && outputDevice->isWritable() && opusDecoder) {
                    opus_int16 outPcm[960 * 2];
                    int decodedSamples = opus_decode(
                        opusDecoder,
                        reinterpret_cast<const unsigned char*>(datagram.constData()),
                        datagram.size(), outPcm, 960, 0);
                    if (decodedSamples > 0) {
                        qint64 byteSize = decodedSamples * 2 * static_cast<int>(sizeof(opus_int16));
                        outputDevice->write(reinterpret_cast<const char*>(outPcm), byteSize);
                    }
                }
            }
        });
    }
}
#endif

void AudioRelayManager::stopAll() {
#ifdef HAVE_QT_MULTIMEDIA
    if (audioInput) {
        audioInput->stop();
        delete audioInput;
        audioInput = nullptr;
    }
    if (audioOutput) {
        audioOutput->stop();
        delete audioOutput;
        audioOutput = nullptr;
    }
#endif
    if (opusEncoder) {
        opus_encoder_destroy(opusEncoder);
        opusEncoder = nullptr;
    }
    if (opusDecoder) {
        opus_decoder_destroy(opusDecoder);
        opusDecoder = nullptr;
    }
    pcmAccumulator.clear();
    inputDevice = nullptr;
    outputDevice = nullptr;
    if (udpSocket) {
        udpSocket->close();
        disconnect(udpSocket, &QUdpSocket::readyRead, this, nullptr);
    }
}
