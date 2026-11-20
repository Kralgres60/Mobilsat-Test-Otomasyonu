/**
 * @file data_stream.cpp
 * @brief Implementation of DataStream pure function-pointer communication stream.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "data_stream.h"
#include <QTimer>
#include <memory>

DataStream::DataStream(QObject *parent)
    : QObject(parent) {
}

DataStream::DataStream(const DrxTransportOps &ops, QObject *parent)
    : QObject(parent), m_ops(ops) {
}

DataStream::DataStream(SendFn sendFn,
                       ConnectFn connectFn,
                       DisconnectFn disconnectFn,
                       IsConnectedFn isConnectedFn,
                       TuneFn tuneFn,
                       StatusFn statusFn,
                       const QString &name,
                       QObject *parent)
    : QObject(parent) {
    m_ops.name = name;
    m_ops.sendFn = std::move(sendFn);
    m_ops.connectFn = std::move(connectFn);
    m_ops.disconnectFn = std::move(disconnectFn);
    m_ops.isConnectedFn = std::move(isConnectedFn);
    m_ops.tuneFn = std::move(tuneFn);
    m_ops.statusFn = std::move(statusFn);
}

void DataStream::setTransportOps(const DrxTransportOps &ops) {
    m_ops = ops;
}

void DataStream::bindMock(double powerDbm, double cnDb, double ber, bool locked) {
    auto state = std::make_shared<bool>(false);
    auto lastFreq = std::make_shared<int>(11054);
    auto lastPol = std::make_shared<QString>("V");

    DrxTransportOps ops;
    ops.name = "Mock";
    ops.connectFn = [this, state]() {
        *state = true;
        emit deviceConnected("mock://drx9000");
        emit logMessage("[Mock] DRX9000 mock transport connected.", false);
        return true;
    };
    ops.disconnectFn = [this, state]() {
        if (!*state) return;
        *state = false;
        emit deviceDisconnected();
        emit logMessage("[Mock] DRX9000 mock transport disconnected.", false);
    };
    ops.isConnectedFn = [state]() { return *state; };
    ops.isReadyFn = [state]() { return *state; };
    ops.sendFn = [this, state](const QByteArray &data) {
        if (!*state) return;
        emit logMessage(QString("[Mock TX] %1").arg(QString::fromUtf8(data).trimmed()), false);
    };
    ops.tuneFn = [this, state, lastFreq, lastPol](int freqMhz, int symbolRate, const QString &pol) {
        if (!*state) return;
        *lastFreq = freqMhz;
        *lastPol = pol;
        emit logMessage(QString("[Mock Tune] %1 MHz | %2 kS/s | %3").arg(freqMhz).arg(symbolRate).arg(pol), false);
        QTimer::singleShot(40, this, &DataStream::requestStatus);
    };
    ops.statusFn = [this, state, lastFreq, lastPol, powerDbm, cnDb, ber, locked]() {
        if (!*state) return;
        const QDateTime now = QDateTime::currentDateTime();
        emit timestampReceived(now, now, now.toString("hh:mm:ss - dd/MM/yyyy"));
        emit statusReceived(locked, locked, locked, QString("%1 MHz %2").arg(*lastFreq).arg(*lastPol));
        emit telemetryReceived(powerDbm, cnDb, ber, cnDb - 1.2);
    };
    ops.deviceInfoFn = [this, state]() {
        if (!*state) return;
        emit deviceInfoReceived("sw_version", "MOCK-v1.0");
        emit deviceInfoReceived("device_id", "DRX9000-MOCK");
    };
    m_ops = ops;
}

bool DataStream::connectDevice() {
    return m_ops.connectFn ? m_ops.connectFn() : false;
}

void DataStream::disconnectDevice() {
    if (m_ops.disconnectFn) m_ops.disconnectFn();
}

bool DataStream::isConnected() const {
    return m_ops.isConnectedFn ? m_ops.isConnectedFn() : false;
}

bool DataStream::isDeviceReady() const {
    return m_ops.isReadyFn ? m_ops.isReadyFn() : isConnected();
}

void DataStream::sendData(const QByteArray &data) {
    if (m_ops.sendFn) m_ops.sendFn(data);
}

void DataStream::sendTune(int freqMhz, int symbolRate, const QString &polarity) {
    if (m_ops.tuneFn) {
        m_ops.tuneFn(freqMhz, symbolRate, polarity);
    } else if (m_ops.sendFn) {
        const QString cmd = QString("tune %1 %2 %3\n").arg(freqMhz).arg(symbolRate).arg(polarity);
        m_ops.sendFn(cmd.toUtf8());
    }
}

void DataStream::requestStatus() {
    if (m_ops.statusFn) m_ops.statusFn();
}

void DataStream::requestDeviceInfo() {
    if (m_ops.deviceInfoFn) m_ops.deviceInfoFn();
}

void DataStream::requestChannelList() {
    if (m_ops.channelListFn) m_ops.channelListFn();
}

void DataStream::selectChannel(int channelNo) {
    if (m_ops.selectChannelFn) m_ops.selectChannelFn(channelNo);
}

bool DataStream::hasChannelSelectionSupport() const {
    return static_cast<bool>(m_ops.channelListFn) && static_cast<bool>(m_ops.selectChannelFn);
}

void DataStream::sendRaw(const QString &command) {
    sendData(command.toUtf8());
}

void DataStream::pushStatus(bool signalLocked, bool videoLocked, bool audioLocked, const QString &channelName) {
    emit statusReceived(signalLocked, videoLocked, audioLocked, channelName);
}

void DataStream::pushTelemetry(double powerDbm, double cnDb, double ber, double ebN0Db) {
    emit telemetryReceived(powerDbm, cnDb, ber, ebN0Db);
}

void DataStream::pushDeviceInfo(const QString &key, const QString &value) {
    emit deviceInfoReceived(key, value);
}

void DataStream::pushTimestamp(const QDateTime &deviceTime, const QDateTime &hostTime, const QString &rawTimeStr) {
    emit timestampReceived(deviceTime, hostTime, rawTimeStr);
}

void DataStream::pushChannelList(const QVector<QPair<int, QString>> &channels) {
    emit channelListReceived(channels);
}

void DataStream::pushChannelSelected(bool ok, const QString &details) {
    emit channelSelected(ok, details);
}

void DataStream::pushLog(const QString &message, bool isError) {
    emit logMessage(message, isError);
}
