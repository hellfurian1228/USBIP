#include "audiorelaymanager.h"

AudioRelayManager::AudioRelayManager(QObject *parent)
    : QObject(parent),
      udpSocket(nullptr),
      audioInput(nullptr),
      audioOutput(nullptr),
      inputDevice(nullptr),
      outputDevice(nullptr) {
    udpSocket = new QUdpSocket(this);
}

AudioRelayManager::~AudioRelayManager() {
    stopAll();
}

QStringList AudioRelayManager::getAvailableInputDevices() {
    QStringList list;
    for (const QAudioDevice &device : QMediaDevices::audioInputs()) {
        list << device.description();
    }
    return list;
}

QStringList AudioRelayManager::getAvailableOutputDevices() {
    QStringList list;
    for (const QAudioDevice &device : QMediaDevices::audioOutputs()) {
        list << device.description();
    }
    return list;
}

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

void AudioRelayManager::startStreaming(const QAudioDevice& device, const QHostAddress& targetIp, quint16 targetPort, int sampleRate, int channelCount) {
    if (audioInput) {
        audioInput->stop();
        delete audioInput;
        audioInput = nullptr;
    }
    inputDevice = nullptr;

    QAudioFormat format;
    format.setSampleRate(sampleRate);
    format.setChannelCount(channelCount);
    format.setSampleFormat(QAudioFormat::Int16);

    audioInput = new QAudioSource(device, format, this);
    inputDevice = audioInput->start();

    if (inputDevice) {
        connect(inputDevice, &QIODevice::readyRead, this, [this, targetIp, targetPort]() {
            if (inputDevice && udpSocket) {
                QByteArray data = inputDevice->readAll();
                if (!data.isEmpty()) {
                    udpSocket->writeDatagram(data, targetIp, targetPort);
                }
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
                if (outputDevice && outputDevice->isOpen() && outputDevice->isWritable()) {
                    outputDevice->write(datagram);
                }
            }
        });
    }
}

void AudioRelayManager::stopAll() {
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
    inputDevice = nullptr;
    outputDevice = nullptr;
    if (udpSocket) {
        udpSocket->close();
        disconnect(udpSocket, &QUdpSocket::readyRead, this, nullptr);
    }
}
