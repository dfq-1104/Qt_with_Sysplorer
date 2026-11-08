#include "remote_link.h"
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMessageAuthenticationCode>
#include <QNetworkProxy>
#include <QUuid>
#include <QtEndian>

namespace {
QByteArray mac(const QByteArray& key,const QByteArray& data) {
    return QMessageAuthenticationCode::hash(data,key,QCryptographicHash::Sha256).toHex();
}
bool equal(const QByteArray& a,const QByteArray& b) {
    if(a.size()!=b.size())return false;
    unsigned char diff=0;for(int i=0;i<a.size();++i)diff|=static_cast<unsigned char>(a[i]^b[i]);
    return diff==0;
}
QString nonce(){return QUuid::createUuid().toString(QUuid::WithoutBraces)+QUuid::createUuid().toString(QUuid::WithoutBraces);}
}
RemoteLink::RemoteLink(QTcpSocket* socket,bool server,const QString& key,const QString& identity,const QString& session,QObject* parent)
    :QObject(parent),m_socket(socket),m_server(server),m_key(key),m_client(identity),m_session(session){
    // Sysplorer endpoints are direct TCP services, independent of web proxies.
    if(!m_server)m_socket->setProxy(QNetworkProxy::NoProxy);
    m_socket->setParent(this);m_socket->setReadBufferSize(MaxFrame*2);
    m_timeout.setSingleShot(true);m_timeout.setInterval(10000);
    connect(&m_timeout,&QTimer::timeout,this,[this]{reject(QStringLiteral("AUTH_TIMEOUT"));});
    connect(m_socket,&QTcpSocket::readyRead,this,&RemoteLink::read);
    connect(m_socket,&QTcpSocket::disconnected,this,[this]{
        const bool authenticated=m_ready;m_timeout.stop();m_ready=false;
        if(!m_server&&!m_failed&&m_started&&!authenticated){
            reject(m_serverNonce.isEmpty()?QStringLiteral("CLOSED_BEFORE_CHALLENGE"):QStringLiteral("AUTH_CONNECTION_CLOSED"));
        }
        emit disconnected();
    });
    connect(m_socket,QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),this,[this](QAbstractSocket::SocketError){
        if(!m_failed&&m_socket->error()!=QAbstractSocket::RemoteHostClosedError)reject(m_socket->errorString());
    });
}
void RemoteLink::start(){
    if(m_started)return;m_started=true;m_timeout.start();
    if(m_key.size()<32){reject(QStringLiteral("ACCESS_KEY_TOO_SHORT"));return;}
    if(m_server){m_serverNonce=nonce();write({{"type","challenge"},{"protocol","battery-tcp/1"},{"nonce",m_serverNonce},{"session",m_session}});}
    if(m_socket->bytesAvailable())read();
}
QByteArray RemoteLink::proof(const QByteArray& purpose) const {
    return mac(m_key.toUtf8(),purpose+'\n'+m_serverNonce.toUtf8()+'\n'+m_clientNonce.toUtf8()+'\n'+m_client.toUtf8()+'\n'+m_session.toUtf8());
}
bool RemoteLink::write(const QJsonObject& value){
    if(m_socket->state()!=QAbstractSocket::ConnectedState)return false;
    const auto bytes=QJsonDocument(value).toJson(QJsonDocument::Compact);
    if(bytes.isEmpty()||bytes.size()>MaxFrame||m_socket->bytesToWrite()>MaxFrame*2){reject(QStringLiteral("FRAME_LIMIT"));return false;}
    QByteArray frame(4,0);qToBigEndian<quint32>(quint32(bytes.size()),reinterpret_cast<uchar*>(frame.data()));frame+=bytes;
    return m_socket->write(frame)==frame.size();
}
bool RemoteLink::send(const QJsonObject& value){
    if(!m_ready)return false;
    const auto data=QJsonDocument(value).toJson(QJsonDocument::Compact).toBase64();
    const auto n=QByteArray::number(++m_sent);const auto direction=m_server?QByteArray("server"):QByteArray("client");
    return write({{"n",QString::fromLatin1(n)},{"payload",QString::fromLatin1(data)},
        {"mac",QString::fromLatin1(mac(m_channelKey,direction+'\n'+n+'\n'+data))}});
}
void RemoteLink::read(){
    m_buffer+=m_socket->readAll();
    if(m_buffer.size()>MaxFrame*2){reject(QStringLiteral("FRAME_LIMIT"));return;}
    while(m_buffer.size()>=4&&!m_failed){
        const auto size=qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(m_buffer.constData()));
        if(!size||size>MaxFrame){reject(QStringLiteral("INVALID_FRAME_LENGTH"));return;}
        if(m_buffer.size()<int(size)+4)return;
        const auto bytes=m_buffer.mid(4,int(size));m_buffer.remove(0,int(size)+4);
        QJsonParseError error;const auto doc=QJsonDocument::fromJson(bytes,&error);
        if(error.error!=QJsonParseError::NoError||!doc.isObject()){reject(QStringLiteral("INVALID_JSON"));return;}
        accept(doc.object());
    }
}
void RemoteLink::accept(const QJsonObject& value){
    if(!m_ready){
        if(m_server){
            if(value["type"].toString()!="authenticate"||value["protocol"].toString()!="battery-tcp/1"){reject("PROTOCOL_MISMATCH");return;}
            m_client=value["client"].toString();m_clientNonce=value["nonce"].toString();
            if(QUuid(m_client).isNull()||m_clientNonce.size()<32||m_clientNonce.size()>128||!equal(value["proof"].toString().toLatin1(),proof("client"))){reject("AUTH_FAILED");return;}
            m_channelKey=proof("channel");
            if(!write({{"type","authenticated"},{"proof",QString::fromLatin1(proof("server"))}}))return;
        }else if(value["type"].toString()=="challenge"&&m_serverNonce.isEmpty()){
            if(value["protocol"].toString()!="battery-tcp/1"){reject("PROTOCOL_MISMATCH");return;}
            m_serverNonce=value["nonce"].toString();m_session=value["session"].toString();m_clientNonce=nonce();
            if(m_serverNonce.size()<32||m_serverNonce.size()>128||QUuid(m_session).isNull()){reject("INVALID_CHALLENGE");return;}
            write({{"type","authenticate"},{"protocol","battery-tcp/1"},{"client",m_client},{"nonce",m_clientNonce},{"proof",QString::fromLatin1(proof("client"))}});return;
        }else if(value["type"].toString()=="authenticated"&&!m_serverNonce.isEmpty()){
            if(!equal(value["proof"].toString().toLatin1(),proof("server"))){reject("AUTH_FAILED");return;}
            m_channelKey=proof("channel");
        }else {reject("AUTH_FAILED");return;}
        m_ready=true;m_timeout.stop();m_key.clear();emit ready();return;
    }
    bool valid=false;const auto n=value["n"].toString().toULongLong(&valid);const auto data=value["payload"].toString().toLatin1();
    const auto direction=m_server?QByteArray("client"):QByteArray("server");
    if(!valid||n!=m_received+1||!equal(value["mac"].toString().toLatin1(),mac(m_channelKey,direction+'\n'+QByteArray::number(n)+'\n'+data))){reject("INVALID_SIGNATURE_OR_SEQUENCE");return;}
    const auto bytes=QByteArray::fromBase64(data);QJsonParseError error;const auto doc=QJsonDocument::fromJson(bytes,&error);
    if(error.error!=QJsonParseError::NoError||!doc.isObject()){reject("INVALID_PAYLOAD");return;}
    m_received=n;emit message(doc.object());
}
void RemoteLink::reject(const QString& reason){
    if(m_failed)return;m_failed=true;m_ready=false;m_timeout.stop();emit failed(reason);m_socket->abort();
}
