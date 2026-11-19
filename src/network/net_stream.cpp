/**
 * @file net_stream.cpp
 * @brief Implementation of the transport-agnostic NetStream abstraction layer.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "net_stream.h"

// --- NetClientStream ---

NetClientStream::NetClientStream(QObject *parent) : QObject(parent) {
}

NetClientStream::NetClientStream(const NetClientOps &ops, QObject *parent)
    : QObject(parent), m_ops(ops) {
}

void NetClientStream::bind(const NetClientOps &ops) {
    m_ops = ops;
}

void NetClientStream::connectToHost(const QString &host, quint16 port) {
    if (m_ops.connectToHost) {
        m_ops.connectToHost(m_ops.ctx, host, port);
    }
}

void NetClientStream::disconnectFromHost() {
    if (m_ops.disconnectFromHost) {
        m_ops.disconnectFromHost(m_ops.ctx);
    }
}

bool NetClientStream::isConnected() const {
    return m_ops.isConnected ? m_ops.isConnected(m_ops.ctx) : false;
}

bool NetClientStream::write(const QByteArray &data) {
    return m_ops.write ? m_ops.write(m_ops.ctx, data) : false;
}

QString NetClientStream::errorString() const {
    return m_ops.errorString ? m_ops.errorString(m_ops.ctx) : QString();
}

// --- NetServerStream ---

NetServerStream::NetServerStream(QObject *parent) : QObject(parent) {
}

NetServerStream::NetServerStream(const NetServerOps &ops, QObject *parent)
    : QObject(parent), m_ops(ops) {
}

void NetServerStream::bind(const NetServerOps &ops) {
    m_ops = ops;
}

bool NetServerStream::start(quint16 port) {
    return m_ops.start ? m_ops.start(m_ops.ctx, port) : false;
}

void NetServerStream::stop() {
    if (m_ops.stop) {
        m_ops.stop(m_ops.ctx);
    }
}

bool NetServerStream::isListening() const {
    return m_ops.isListening ? m_ops.isListening(m_ops.ctx) : false;
}

bool NetServerStream::writeToPeer(quint64 peerId, const QByteArray &data) {
    return m_ops.writeToPeer ? m_ops.writeToPeer(m_ops.ctx, peerId, data) : false;
}

void NetServerStream::disconnectPeer(quint64 peerId, bool abortImmediately) {
    if (m_ops.disconnectPeer) {
        m_ops.disconnectPeer(m_ops.ctx, peerId, abortImmediately);
    }
}

QString NetServerStream::errorString() const {
    return m_ops.errorString ? m_ops.errorString(m_ops.ctx) : QString();
}

// --- NetDatagramStream ---

NetDatagramStream::NetDatagramStream(QObject *parent) : QObject(parent) {
}

NetDatagramStream::NetDatagramStream(const NetDatagramOps &ops, QObject *parent)
    : QObject(parent), m_ops(ops) {
}

void NetDatagramStream::bind(const NetDatagramOps &ops) {
    m_ops = ops;
}

bool NetDatagramStream::bindPort(quint16 port) {
    return m_ops.bind ? m_ops.bind(m_ops.ctx, port) : false;
}

void NetDatagramStream::close() {
    if (m_ops.close) {
        m_ops.close(m_ops.ctx);
    }
}

bool NetDatagramStream::writeBroadcast(const QByteArray &data, quint16 port) {
    return m_ops.writeBroadcast ? m_ops.writeBroadcast(m_ops.ctx, data, port) : false;
}
