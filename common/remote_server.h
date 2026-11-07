#pragma once
#include <QObject>
#include <QTcpServer>
#include <QSet>
#include "remote_link.h"

class RemoteServer : public QObject {
    Q_OBJECT
public:
    explicit RemoteServer(QObject* parent=nullptr);
    bool start(const QString& configPath,const QString& session);
    void close();
    bool isListening() const {return m_server.isListening();}
    quint16 port() const {return m_server.serverPort();}
    QString workspace() const {return m_workspace;}
signals:
    void request(RemoteLink* link,const QJsonObject& value);
private:
    void publish(const QString& error=QString());
    QTcpServer m_server;
    QSet<RemoteLink*> m_links;
    QString m_key,m_session,m_workspace,m_statusFile,m_address;
    bool m_enabled=false;
    int m_configuredPort=0;
};
