/**
 * @file serial_worker.cpp
 * @brief Implementation of RS232 Serial Worker and hardware state tracker.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "serial_worker.h"
#include "protocol_commands.h"
#include <QDebug>

SerialWorker::SerialWorker(QObject *parent) : QObject(parent) {
    connect(&m_serial, &QSerialPort::readyRead, this, &SerialWorker::onReadyRead);
    connect(&m_serial, &QSerialPort::errorOccurred, this, &SerialWorker::onErrorOccurred);

    connect(&m_protocol, &ProtocolHandler::statusParsed, this, &SerialWorker::onStatusParsed);
    connect(&m_protocol, &ProtocolHandler::deviceInfoParsed, this, &SerialWorker::onDeviceInfoParsed);
    connect(&m_protocol, &ProtocolHandler::interactivePromptDetected, this, &SerialWorker::onInteractivePromptDetected);

    connect(&m_protocol, &ProtocolHandler::rfTelemetryParsed, 
            this, [this](const QString &/*channel*/, double /*freq*/, const QString &/*pol*/, double powDbm, double cnDb, double ber, double /*ebn0*/) {
        emit telemetryReceived(powDbm, cnDb, ber);
    });
}

SerialWorker::~SerialWorker() {
    closePort();
}

QString SerialWorker::readyStateString() const {
    switch (m_readyState) {
        case DeviceReadyState::Disconnected: return "Disconnected";
        case DeviceReadyState::PortOpenWaitingDevice: return "Waiting for Device";
        case DeviceReadyState::Booting: return "Device Booting...";
        case DeviceReadyState::Ready: return "Device Ready";
    }
    return "Unknown";
}

void SerialWorker::setReadyState(DeviceReadyState newState, const QString &desc) {
    if (m_readyState != newState) {
        m_readyState = newState;
        emit deviceReadyStateChanged(newState, desc);
        emit logMessage(QString("[DEVICE STATE] %1 (%2)").arg(readyStateString(), desc), false);
    }
}

QStringList SerialWorker::availablePorts() {
    QStringList ports;
    const auto list = QSerialPortInfo::availablePorts();
    for (const auto &info : list) {
        ports << info.portName();
    }
    return ports;
}

bool SerialWorker::openPort(const QString &portName, int baudRate) {
    if (m_serial.isOpen()) {
        m_serial.close();
    }

    m_serial.setPortName(portName);
    m_serial.setBaudRate(baudRate);
    m_serial.setDataBits(QSerialPort::Data8);
    m_serial.setParity(QSerialPort::NoParity);
    m_serial.setStopBits(QSerialPort::OneStop);
    m_serial.setFlowControl(QSerialPort::NoFlowControl);

    if (m_serial.open(QIODevice::ReadWrite)) {
        m_rxBuffer.clear();
        setReadyState(DeviceReadyState::PortOpenWaitingDevice, "Port opened. Awaiting device heartbeat...");
        emit portOpened(portName);
        emit logMessage(QString("Port Opened: %1 (%2 baud)").arg(portName).arg(baudRate), false);

        // Immediate status query if device is already active
        sendRaw("getstatus\n");
        return true;
    } else {
        QString err = m_serial.errorString();
        setReadyState(DeviceReadyState::Disconnected, err);
        emit portError(err);
        emit logMessage(QString("Failed to open port: %1 - %2").arg(portName, err), true);
        return false;
    }
}

void SerialWorker::closePort() {
    if (m_serial.isOpen()) {
        m_serial.close();
        setReadyState(DeviceReadyState::Disconnected, "Port closed");
        emit portClosed();
        emit logMessage("Port closed.", false);
    }
}

bool SerialWorker::isConnected() const {
    return m_serial.isOpen();
}

void SerialWorker::sendTune(int freqMHz, int symbolRate, const QString &polarity) {
    if (!isConnected()) return;
    QString cmd = QString("tune %1 %2 %3\n").arg(freqMHz).arg(symbolRate).arg(polarity.toLower());
    sendRaw(cmd);
}

void SerialWorker::requestStatus() {
    if (!isConnected()) return;
    sendRaw("getstatus\n");
}

void SerialWorker::startTunerLog() {
    if (!isConnected()) return;
    sendRaw("tunerlog on\n");
}

void SerialWorker::stopTunerLog() {
    if (!isConnected()) return;
    sendRaw("tunerlog off\n");
}

void SerialWorker::sendRaw(const QString &command) {
    if (!isConnected()) return;
    QByteArray data = command.toUtf8();
    m_serial.write(data);
    emit logMessage(command.trimmed(), true);
}

void SerialWorker::onReadyRead() {
    m_rxBuffer.append(m_serial.readAll());
    m_protocol.processBuffer(m_rxBuffer);
}

void SerialWorker::onErrorOccurred(QSerialPort::SerialPortError error) {
    if (error == QSerialPort::ResourceError || error == QSerialPort::PermissionError) {
        emit portError(m_serial.errorString());
        closePort();
    }
}

void SerialWorker::onDeviceInfoParsed(const QString &key, const QString &value) {
    if (key == "swVer") m_swVer = value;
    else if (key == "hwVer") m_hwVer = value;
    else if (key == "model") m_model = value;

    emit deviceInfoReceived(key, value);

    // Bootloader banner signals device is in booting phase
    if (m_readyState != DeviceReadyState::Ready) {
        setReadyState(DeviceReadyState::Booting, QString("%1: %2 detected").arg(key, value));
    }
}

void SerialWorker::onInteractivePromptDetected(const QString &promptLine) {
    emit devicePromptDetected(promptLine);
    // CLI prompt signifies boot complete and ready for command injection
    setReadyState(DeviceReadyState::Ready, "Command prompt detected (-->)");
}

void SerialWorker::onStatusParsed(bool signalLocked, bool videoProducing, bool audioProducing) {
    // Valid status response confirms active receiver readiness
    setReadyState(DeviceReadyState::Ready, "Status response received");
    emit statusReceived(signalLocked, videoProducing, audioProducing);
}
