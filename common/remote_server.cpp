#include "remote_server.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

RemoteServer::RemoteServer(QObject* parent):QObject(parent){
    connect(&m_server,&QTcpServer::newConnection,this,[this]{
        while(m_server.hasPendingConnections()){
            auto socket=m_server.nextPendingConnection();
            if(m_links.size()>=8){socket->abort();socket->deleteLater();continue;}
            auto link=new RemoteLink(socket,true,m_key,QString(),m_session,this);m_links.insert(link);
            connect(link,&RemoteLink::message,this,[this,link](const QJsonObject& value){emit request(link,value);});
            connect(link,&RemoteLink::failed,this,[this](const QString& reason){publish(reason);});
            connect(link,&RemoteLink::ready,this,[this]{publish();});
            connect(link,&RemoteLink::disconnected,this,[this,link]{m_links.remove(link);link->deleteLater();});
            link->start();
        }
    });
}
bool RemoteServer::start(const QString& configPath,const QString& session){
    close();QSettings config(configPath,QSettings::IniFormat);config.setIniCodec("UTF-8");
    m_session=session;m_enabled=config.value("Remote/Enabled",false).toBool();
    m_workspace=config.value("Remote/Workspace").toString();
    m_statusFile=config.value("Remote/StatusFile",QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)+"/BatteryConsole/remote/"+QString::number(QCoreApplication::applicationPid())+".json").toString();
    if(!m_enabled){publish();return true;}
    m_key=config.value("Remote/AccessKey").toString();m_address=config.value("Remote/ListenAddress","127.0.0.1").toString();
    m_configuredPort=config.value("Remote/Port",39001).toInt();m_workspace=config.value("Remote/Workspace").toString();
    QHostAddress address;
    if(m_key.size()<32||m_configuredPort<1||m_configuredPort>65535||!address.setAddress(m_address)){
        publish("INVALID_CONFIG: AccessKey must have at least 32 characters; a literal listen address and port 1-65535 are required");return false;
    }
    if(!m_workspace.isEmpty()){
        if(!QFileInfo(m_workspace).isAbsolute()){publish("WORKSPACE_MUST_BE_ABSOLUTE");return false;}
        for(const auto c:m_workspace)if(c.unicode()>127){publish("ASCII_WORKSPACE_REQUIRED");return false;}
        if(!QDir().mkpath(m_workspace)){publish("WORKSPACE_NOT_WRITABLE");return false;}
    }
    const bool ok=m_server.listen(address,quint16(m_configuredPort));publish(ok?QString():m_server.errorString());return ok;
}
void RemoteServer::close(){
    m_server.close();const auto links=m_links;m_links.clear();
    for(auto link:links){disconnect(link,nullptr,this,nullptr);link->socket()->abort();link->deleteLater();}
    publish();
}
void RemoteServer::publish(const QString& error){
    if(m_statusFile.isEmpty())return;QDir().mkpath(QFileInfo(m_statusFile).absolutePath());QSaveFile file(m_statusFile);
    if(file.open(QIODevice::WriteOnly)){
        file.write(QJsonDocument(QJsonObject{{"protocol","battery-tcp/1"},{"enabled",m_enabled},{"listening",m_server.isListening()},
            {"pid",double(QCoreApplication::applicationPid())},{"address",m_address},{"port",m_configuredPort},{"error",error},
            {"updated",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}}).toJson());file.commit();
    }
}
