#pragma once
#include <QObject>
#include <QTcpSocket>
#include <QJsonObject>
#include <QTimer>

// Length-prefixed JSON with challenge authentication and signed, ordered frames.
// Contents are authenticated, not encrypted: deploy on a trusted LAN or VPN.
class RemoteLink : public QObject {
    Q_OBJECT
public:
    explicit RemoteLink(QTcpSocket* socket, bool server, const QString& key,
                        const QString& identity, const QString& session = QString(), QObject* parent = nullptr);
    void start();
    bool send(const QJsonObject& message);
    bool isReady() const { return m_ready; }
    QString clientId() const { return m_client; }
    QString session() const { return m_session; }
    QTcpSocket* socket() const { return m_socket; }
    static const int MaxFrame = 1024 * 1024;
signals:
    void ready();
    void message(const QJsonObject& value);
    void failed(const QString& reason);
    void disconnected();
private:
    void read();
    void accept(const QJsonObject& value);
    bool write(const QJsonObject& value);
    void reject(const QString& reason);
    QByteArray proof(const QByteArray& purpose) const;
    QTcpSocket* m_socket;
    bool m_server=false,m_ready=false,m_failed=false,m_started=false;
    QString m_key,m_client,m_session,m_serverNonce,m_clientNonce;
    QByteArray m_channelKey,m_buffer;
    quint64 m_sent=0,m_received=0;
    QTimer m_timeout;
};
