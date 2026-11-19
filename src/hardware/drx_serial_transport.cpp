/**
 * @file drx_serial_transport.cpp
 * @brief Implementation of RS232 UART serial transport inheriting DataTransfer.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "drx_serial_transport.h"
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QDebug>

RS232::RS232(QObject *parent)
    : DataTransfer(parent) {
    connect(&m_serial, &QSerialPort::readyRead, this, &RS232::onReadyRead);
    connect(&m_serial, &QSerialPort::errorOccurred, this, &RS232::onErrorOccurred);
}

RS232::RS232(const QString &portName, int baudRate, QObject *parent)
    : DataTransfer(parent), m_portName(portName), m_baudRate(baudRate) {
    connect(&m_serial, &QSerialPort::readyRead, this, &RS232::onReadyRead);
    connect(&m_serial, &QSerialPort::errorOccurred, this, &RS232::onErrorOccurred);
}

RS232::~RS232() {
    closePort();
}

QString RS232::readyStateString() const {
    switch (m_readyState) {
        case DeviceReadyState::Disconnected: return "Bağlantı Yok";
        case DeviceReadyState::PortOpenWaitingDevice: return "Cihaz Bekleniyor";
        case DeviceReadyState::Booting: return "Açılıyor (Booting)";
        case DeviceReadyState::Ready: return "Hazır (Ready)";
    }
    return "Bilinmiyor";
}

void RS232::setReadyState(DeviceReadyState newState, const QString &desc) {
    if (m_readyState != newState) {
        m_readyState = newState;
        emit deviceReadyStateChanged(newState, desc);
        emit logMessage(QString("[RS232 Durum] %1 (%2)").arg(readyStateString(), desc), false);
    }
}

QStringList RS232::availablePorts() {
    QStringList ports;
    const auto list = QSerialPortInfo::availablePorts();
    for (const auto &info : list) {
        ports << info.portName();
    }
    return ports;
}

bool RS232::connectDevice() {
    return openPort(m_portName, m_baudRate);
}

void RS232::disconnectDevice() {
    closePort();
}

bool RS232::isConnected() const {
    return m_serial.isOpen();
}

bool RS232::openPort(const QString &portName, int baudRate) {
    if (m_serial.isOpen()) {
        m_serial.close();
    }

    m_portName = portName;
    m_baudRate = baudRate;

    m_serial.setPortName(portName);
    m_serial.setBaudRate(baudRate);
    m_serial.setDataBits(QSerialPort::Data8);
    m_serial.setParity(QSerialPort::NoParity);
    m_serial.setStopBits(QSerialPort::OneStop);
    m_serial.setFlowControl(QSerialPort::NoFlowControl);

    if (m_serial.open(QIODevice::ReadWrite)) {
        m_rxBuffer.clear();
        setReadyState(DeviceReadyState::PortOpenWaitingDevice, "Port açıldı, cihaz bekleniyor...");
        emit portOpened(portName);
        emit deviceConnected(portName);
        emit logMessage(QString("Seri Port Açıldı: %1 (%2 baud)").arg(portName).arg(baudRate), false);

        sendRaw("getstatus\n");
        return true;
    } else {
        QString err = m_serial.errorString();
        setReadyState(DeviceReadyState::Disconnected, err);
        emit portError(err);
        emit logMessage(QString("Seri port açılamadı (%1): %2").arg(portName, err), true);
        return false;
    }
}

void RS232::closePort() {
    if (m_serial.isOpen()) {
        m_serial.close();
        setReadyState(DeviceReadyState::Disconnected, "Port kapatıldı");
        emit portClosed();
        emit deviceDisconnected();
        emit logMessage("Seri port kapatıldı.", false);
    }
}

void RS232::setPortConfig(const QString &portName, int baudRate) {
    m_portName = portName;
    m_baudRate = baudRate;
}

void RS232::sendData(const QByteArray &data) {
    if (!isConnected()) return;
    m_serial.write(data);
    emit logMessage(QString::fromUtf8(data).trimmed(), true);
}

void RS232::sendTune(int freqMhz, int symbolRate, const QString &polarity) {
    if (!isConnected()) return;
    QString cmd = DrxProtocolConfig::instance().formatSerialTune(freqMhz, symbolRate, polarity);
    sendRaw(cmd);
}

void RS232::requestStatus() {
    if (!isConnected()) return;
    sendRaw(DrxProtocolConfig::instance().serialStatusCmd());
}

void RS232::requestDeviceInfo() {
    if (!isConnected()) return;
    sendRaw(DrxProtocolConfig::instance().serialDeviceInfoCmd());
}

void RS232::startTunerLog() {
    if (!isConnected()) return;
    sendRaw(DrxProtocolConfig::instance().serialTunerLogOnCmd());
}

void RS232::stopTunerLog() {
    if (!isConnected()) return;
    sendRaw(DrxProtocolConfig::instance().serialTunerLogOffCmd());
}

void RS232::sendRaw(const QString &command) {
    sendData(command.toUtf8());
}

void RS232::onReadyRead() {
    m_rxBuffer.append(m_serial.readAll());
    processRxBuffer();
}

void RS232::onErrorOccurred(QSerialPort::SerialPortError error) {
    if (error == QSerialPort::ResourceError || error == QSerialPort::PermissionError) {
        emit portError(m_serial.errorString());
        closePort();
    }
}

void RS232::processRxBuffer() {
    static const QRegularExpression ansiRegex("\x1b\\[[0-9;]*[a-zA-Z]");
    m_rxBuffer.replace('\0', ' ');

    while (m_rxBuffer.contains('\n') || m_rxBuffer.contains('\r')) {
        int indexLf = m_rxBuffer.indexOf('\n');
        int indexCr = m_rxBuffer.indexOf('\r');
        int index = (indexLf != -1 && indexCr != -1) ? qMin(indexLf, indexCr) : (indexLf != -1 ? indexLf : indexCr);

        QByteArray lineBytes = m_rxBuffer.left(index);
        m_rxBuffer.remove(0, index + 1);

        QString line = QString::fromUtf8(lineBytes.constData(), lineBytes.size()).remove(ansiRegex).trimmed();
        if (line.isEmpty()) continue;

        emit logMessage(line, false);

        if (DrxProtocolConfig::instance().isPromptLine(line)) {
            emit devicePromptDetected(line);
            setReadyState(DeviceReadyState::Ready, "Komut istemi tespit edildi (-->)");
        }

        if (DrxProtocolConfig::instance().isStatusResponseLine(line)) {
            bool s = false, v = false, a = false;
            DrxProtocolConfig::instance().parseSerialLocks(line, s, v, a);
            setReadyState(DeviceReadyState::Ready, "Durum yanıtı alındı");
            emit statusReceived(s, v, a);
        } else if (line.contains("POW:", Qt::CaseInsensitive) || line.contains("C/N:", Qt::CaseInsensitive)) {
            double pow = 0, cn = 0, ber = 1.0e-7;
            if (DrxProtocolConfig::instance().parseSerialTelemetry(line, pow, cn, ber) ||
                parseRfTelemetryLine(line, pow, cn, ber)) {
                emit telemetryReceived(pow, cn, ber);
            }
        } else if (line.contains("SW Ver", Qt::CaseInsensitive)) {
            QString val = extractValueAfterKey(line, "SW Ver");
            if (!val.isEmpty()) {
                m_swVer = val;
                emit deviceInfoReceived("sw_version", val);
            }
        } else if (line.contains("HW Ver", Qt::CaseInsensitive)) {
            QString val = extractValueAfterKey(line, "HW Ver");
            if (!val.isEmpty()) {
                m_hwVer = val;
                emit deviceInfoReceived("hw_version", val);
            }
        } else if (line.contains("NETA DRX", Qt::CaseInsensitive)) {
            m_model = line;
            emit deviceInfoReceived("model", line);
        }
    }
}

bool RS232::parseRfTelemetryLine(const QString &line, double &outPow, double &outCn, double &outBer) {
    if (line.contains("POW:", Qt::CaseInsensitive) && line.contains("C/N:", Qt::CaseInsensitive)) {
        outPow = extractValueAfterKey(line, "POW:").remove("dBm", Qt::CaseInsensitive).toDouble();
        outCn = extractValueAfterKey(line, "C/N:").remove("dB", Qt::CaseInsensitive).toDouble();
        outBer = extractValueAfterKey(line, "BER:").toDouble();
        if (outBer <= 0) outBer = 1.0e-7;
        return (outCn != 0 || outPow != 0);
    }
    return false;
}

QString RS232::extractValueAfterKey(const QString &line, const QString &key) {
    int keyPos = line.indexOf(key, 0, Qt::CaseInsensitive);
    if (keyPos == -1) return QString();

    int startPos = keyPos + key.length();
    while (startPos < line.length() && (line[startPos] == ':' || line[startPos] == ' ' || line[startPos] == '\t')) {
        startPos++;
    }
    if (startPos >= line.length()) return QString();

    QString val = line.mid(startPos).trimmed();
    int endPos = val.indexOf(QRegularExpression("[\\s,\\;]"));
    if (endPos != -1) val = val.left(endPos).trimmed();
    return val;
}
