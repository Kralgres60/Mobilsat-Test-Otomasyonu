/**
 * @file drx_data_control.cpp
 * @brief Implementation of DrxDataControl unified function-pointer communication controller.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "drx_data_control.h"
#include "drx_ethernet_transport.h"
#include "drx_serial_transport.h"
#include <QTimer>
#include <memory>

DrxDataControl::DrxDataControl(QObject *parent)
    : QObject(parent) {
}

DrxDataControl::DrxDataControl(const DrxTransportOps &ops, QObject *parent)
    : QObject(parent), m_ops(ops) {
}

void DrxDataControl::clearBoundConnections() {
    for (const auto &conn : m_boundConnections) {
        QObject::disconnect(conn);
    }
    m_boundConnections.clear();
}

void DrxDataControl::setTransportOps(const DrxTransportOps &ops) {
    clearBoundConnections();
    m_ops = ops;
}

void DrxDataControl::bindEthernet(Ethernet *eth) {
    clearBoundConnections();
    if (!eth) {
        m_ops = DrxTransportOps{};
        return;
    }

    DrxTransportOps ops;
    ops.name = "Ethernet";
    ops.connectFn = [eth]() { return eth->connectDevice(); };
    ops.disconnectFn = [eth]() { eth->disconnectDevice(); };
    ops.isConnectedFn = [eth]() { return eth->isConnected(); };
    ops.isReadyFn = [eth]() { return eth->isDeviceReady(); };
    ops.sendFn = [eth](const QByteArray &data) { eth->sendData(data); };
    ops.tuneFn = [eth](int f, int sr, const QString &pol) { eth->sendTune(f, sr, pol); };
    ops.statusFn = [eth]() { eth->requestStatus(); };
    ops.deviceInfoFn = [eth]() { eth->requestDeviceInfo(); };
    m_ops = ops;

    m_boundConnections << connect(eth, &Ethernet::deviceConnected, this, &DrxDataControl::deviceConnected);
    m_boundConnections << connect(eth, &Ethernet::deviceDisconnected, this, &DrxDataControl::deviceDisconnected);
    m_boundConnections << connect(eth, &Ethernet::statusReceived, this, &DrxDataControl::statusReceived);
    m_boundConnections << connect(eth, &Ethernet::telemetryReceived, this, &DrxDataControl::telemetryReceived);
    m_boundConnections << connect(eth, &Ethernet::deviceInfoReceived, this, &DrxDataControl::deviceInfoReceived);
    m_boundConnections << connect(eth, &Ethernet::timestampReceived, this, &DrxDataControl::timestampReceived);
    m_boundConnections << connect(eth, &Ethernet::logMessage, this, &DrxDataControl::logMessage);
}

void DrxDataControl::bindSerial(RS232 *serial) {
    clearBoundConnections();
    if (!serial) {
        m_ops = DrxTransportOps{};
        return;
    }

    DrxTransportOps ops;
    ops.name = "RS232";
    ops.connectFn = [serial]() { return serial->connectDevice(); };
    ops.disconnectFn = [serial]() { serial->disconnectDevice(); };
    ops.isConnectedFn = [serial]() { return serial->isConnected(); };
    ops.isReadyFn = [serial]() { return serial->isDeviceReady(); };
    ops.sendFn = [serial](const QByteArray &data) { serial->sendData(data); };
    ops.tuneFn = [serial](int f, int sr, const QString &pol) { serial->sendTune(f, sr, pol); };
    ops.statusFn = [serial]() { serial->requestStatus(); };
    ops.deviceInfoFn = [serial]() { serial->requestDeviceInfo(); };
    m_ops = ops;

    m_boundConnections << connect(serial, &RS232::deviceConnected, this, &DrxDataControl::deviceConnected);
    m_boundConnections << connect(serial, &RS232::deviceDisconnected, this, &DrxDataControl::deviceDisconnected);
    m_boundConnections << connect(serial, &RS232::statusReceived, this, &DrxDataControl::statusReceived);
    m_boundConnections << connect(serial, &RS232::telemetryReceived, this, &DrxDataControl::telemetryReceived);
    m_boundConnections << connect(serial, &RS232::deviceInfoReceived, this, &DrxDataControl::deviceInfoReceived);
    m_boundConnections << connect(serial, &RS232::timestampReceived, this, &DrxDataControl::timestampReceived);
    m_boundConnections << connect(serial, &RS232::logMessage, this, &DrxDataControl::logMessage);
}

void DrxDataControl::bindMock(double powerDbm, double cnDb, double ber, bool locked) {
    clearBoundConnections();

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
        QTimer::singleShot(40, this, &DrxDataControl::requestStatus);
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

bool DrxDataControl::connectDevice() {
    return m_ops.connectFn ? m_ops.connectFn() : false;
}

void DrxDataControl::disconnectDevice() {
    if (m_ops.disconnectFn) m_ops.disconnectFn();
}

bool DrxDataControl::isConnected() const {
    return m_ops.isConnectedFn ? m_ops.isConnectedFn() : false;
}

bool DrxDataControl::isDeviceReady() const {
    return m_ops.isReadyFn ? m_ops.isReadyFn() : isConnected();
}

void DrxDataControl::sendData(const QByteArray &data) {
    if (m_ops.sendFn) m_ops.sendFn(data);
}

void DrxDataControl::sendTune(int freqMhz, int symbolRate, const QString &polarity) {
    if (m_ops.tuneFn) {
        m_ops.tuneFn(freqMhz, symbolRate, polarity);
    } else if (m_ops.sendFn) {
        const QString cmd = QString("tune %1 %2 %3\n").arg(freqMhz).arg(symbolRate).arg(polarity);
        m_ops.sendFn(cmd.toUtf8());
    }
}

void DrxDataControl::requestStatus() {
    if (m_ops.statusFn) m_ops.statusFn();
}

void DrxDataControl::requestDeviceInfo() {
    if (m_ops.deviceInfoFn) m_ops.deviceInfoFn();
}

void DrxDataControl::sendRaw(const QString &command) {
    sendData(command.toUtf8());
}

void DrxDataControl::pushStatus(bool signalLocked, bool videoLocked, bool audioLocked, const QString &channelName) {
    emit statusReceived(signalLocked, videoLocked, audioLocked, channelName);
}

void DrxDataControl::pushTelemetry(double powerDbm, double cnDb, double ber, double ebN0Db) {
    emit telemetryReceived(powerDbm, cnDb, ber, ebN0Db);
}

void DrxDataControl::pushDeviceInfo(const QString &key, const QString &value) {
    emit deviceInfoReceived(key, value);
}

void DrxDataControl::pushTimestamp(const QDateTime &deviceTime, const QDateTime &hostTime, const QString &rawTimeStr) {
    emit timestampReceived(deviceTime, hostTime, rawTimeStr);
}

void DrxDataControl::pushLog(const QString &message, bool isError) {
    emit logMessage(message, isError);
}
