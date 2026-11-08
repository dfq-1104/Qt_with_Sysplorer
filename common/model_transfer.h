#pragma once
#include <QByteArray>
#include <QCryptographicHash>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <QTextCodec>
#include <QUuid>
#include <cmath>
#include <stdexcept>

// Shared wire contract. No model text is embedded in the host plugin.
namespace ModelTransfer {
const int Limit=1024*1024, UdpChunk=768, TcpChunk=48*1024;
inline QString interfaceName(){return QStringLiteral("battery-console/1");}
inline QString sha(const QByteArray& b){return QString::fromLatin1(QCryptographicHash::hash(b,QCryptographicHash::Sha256).toHex());}
inline void need(bool ok,const char* code){if(!ok)throw std::runtime_error(code);}
inline void validate(const QByteArray& b){
    need(!b.isEmpty()&&b.size()<=Limit,"MODEL_SIZE_INVALID");
    QTextCodec::ConverterState state;
    const auto text=QTextCodec::codecForName("UTF-8")->toUnicode(b.constData(),b.size(),&state);
    need(state.invalidChars==0&&state.remainingChars==0&&!text.contains(QChar(0)),"MODEL_UTF8_INVALID");
    // This product supports one interface. The host parser validates Modelica syntax.
    need(QRegularExpression("\\bpackage\\s+BatterySim\\b").match(text).hasMatch()
         &&QRegularExpression("\\bmodel\\s+SingleRun\\b").match(text).hasMatch(),"MODEL_INTERFACE_INVALID");
}
struct Upload {
    QString id,hash;int size=0,chunkSize=0; qint64 touched=0;
    QMap<int,QByteArray> chunks;QByteArray committed;
};
class Store {
public:
    void expire(qint64 now){for(auto i=items.begin();i!=items.end();)if(now-i->touched>=60000)i=items.erase(i);else ++i;}
    QJsonObject execute(const QString& owner,const QString& command,const QJsonObject& a,int maxChunk,qint64 now){
        expire(now);const auto id=a["uploadId"].toString();need(!QUuid(id).isNull(),"MODEL_UPLOAD_ID_INVALID");
        if(command=="model.begin"){
            need(a["modelInterface"].toString()==interfaceName()&&a["modelTransferVersion"].toInt()==1,"MODEL_INTERFACE_INVALID");
            const double bytes=a["bytes"].toDouble(-1),chunk=a["chunkSize"].toDouble(-1);
            need(std::isfinite(bytes)&&bytes==std::floor(bytes)&&bytes>0&&bytes<=Limit,"MODEL_SIZE_INVALID");
            need(chunk==std::floor(chunk)&&chunk>0&&chunk<=maxChunk,"MODEL_CHUNK_LIMIT");
            const auto hash=a["sha256"].toString();need(QRegularExpression("^[0-9a-f]{64}$").match(hash).hasMatch(),"MODEL_HASH_INVALID");
            if(items.contains(owner)&&items[owner].id==id){
                const auto& old=items[owner];need(old.hash==hash&&old.size==bytes&&old.chunkSize==chunk,"MODEL_UPLOAD_CONFLICT");
            }else{
                need(items.contains(owner)||items.size()<8,"MODEL_UPLOAD_BUSY");
                Upload u;u.id=id;u.hash=hash;u.size=int(bytes);u.chunkSize=int(chunk);items[owner]=u;
            }
        }
        need(items.contains(owner)&&items[owner].id==id,"MODEL_UPLOAD_NOT_FOUND");
        auto& u=items[owner];u.touched=now;
        if(command=="model.abort"){items.remove(owner);return {{"ok",true}};}
        if(command=="model.chunk"){
            const double offset=a["offset"].toDouble(-1);const auto encoded=a["data"].toString().toLatin1();
            need(encoded.size()<=((maxChunk+2)/3)*4,"MODEL_CHUNK_LIMIT");
            const auto data=QByteArray::fromBase64(encoded);
            need(data.toBase64()==encoded,"MODEL_CHUNK_ENCODING");
            need(offset>=0&&offset==std::floor(offset)&&offset<u.size&&int(offset)%u.chunkSize==0,"MODEL_CHUNK_OFFSET");
            need(data.size()==qMin(u.chunkSize,u.size-int(offset)),"MODEL_CHUNK_SIZE");
            need(sha(data)==a["sha256"].toString(),"MODEL_CHUNK_HASH");
            need(!u.chunks.contains(int(offset))||u.chunks[int(offset)]==data,"MODEL_CHUNK_CONFLICT");
            if(!u.committed.isEmpty())need(u.committed.mid(int(offset),data.size())==data,"MODEL_CHUNK_CONFLICT");
            else u.chunks[int(offset)]=data;
        }else if(command=="model.commit"){
            if(u.committed.isEmpty()){
                QByteArray bytes;for(int pos=0;pos<u.size;pos+=u.chunkSize){need(u.chunks.contains(pos),"MODEL_CHUNKS_MISSING");bytes+=u.chunks[pos];}
                need(bytes.size()==u.size&&sha(bytes)==u.hash,"MODEL_HASH_MISMATCH");validate(bytes);u.committed=bytes;u.chunks.clear();
            }
        }else need(command=="model.begin","MODEL_COMMAND_INVALID");
        return {{"ok",true},{"uploadId",id},{"modelHash",u.hash},{"committed",!u.committed.isEmpty()},{"offset",a["offset"]}};
    }
    QByteArray take(const QString& owner,const QString& hash,qint64 now){
        expire(now);need(items.contains(owner)&&items[owner].hash==hash&&!items[owner].committed.isEmpty(),"MODEL_NOT_UPLOADED");
        items[owner].touched=now;return items[owner].committed;
    }
private: QMap<QString,Upload> items;
};
}
