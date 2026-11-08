#pragma once
#include <QByteArray>
#include <QCryptographicHash>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <QUuid>
#include <cmath>
#include <stdexcept>

// Shared wire contract. No model text is embedded in the host plugin.
namespace ModelTransfer {
const int Limit=1024*1024, UdpChunk=768, TcpChunk=48*1024;
inline QString interfaceName(){return QStringLiteral("battery-console/1");}
inline QString sha(const QByteArray& b){return QString::fromLatin1(QCryptographicHash::hash(b,QCryptographicHash::Sha256).toHex());}
inline void need(bool ok,const char* code){if(!ok)throw std::runtime_error(code);}
struct ValidationError : std::runtime_error {
    QString reason; int offset;
    ValidationError(const char* code,const QString& why,int at=-1):std::runtime_error(code),reason(why),offset(at){}
};
inline void validate(const QByteArray& b){
    if(b.isEmpty()||b.size()>Limit)throw ValidationError("MODEL_SIZE_INVALID",QString::fromUtf8("模型必须为 1 字节至 1 MiB"));
    const auto bytes=reinterpret_cast<const unsigned char*>(b.constData());
    auto bad=[](const QString& why,int at){throw ValidationError("MODEL_UTF8_INVALID",why,at);};
    if(b.startsWith("\xff\xfe")||b.startsWith("\xfe\xff"))bad(QString::fromUtf8("检测到 UTF-16；请提供 UTF-8 文件"),0);
    // RFC 3629 byte validation, independent of codec registries, locale and
    // converter state. In particular, reject overlongs, surrogates and >U+10FFFF.
    for(int i=0;i<b.size();){
        const auto c=bytes[i];
        if(c==0)bad(QString::fromUtf8("检测到 NUL 字节（可能是 UTF-16）"),i);
        if(c<0x80){++i;continue;}
        const int n=c>=0xc2&&c<=0xdf?2:c>=0xe0&&c<=0xef?3:c>=0xf0&&c<=0xf4?4:0;
        if(!n)bad(QString::fromUtf8("非法 UTF-8 起始字节"),i);
        if(i+n>b.size())bad(QString::fromUtf8("UTF-8 多字节字符被截断"),i);
        for(int j=1;j<n;++j)if(bytes[i+j]<0x80||bytes[i+j]>0xbf)bad(QString::fromUtf8("非法 UTF-8 延续字节"),i+j);
        const auto next=bytes[i+1];
        if((c==0xe0&&next<0xa0)||(c==0xed&&next>0x9f)||(c==0xf0&&next<0x90)||(c==0xf4&&next>0x8f))bad(QString::fromUtf8("非法 Unicode 码点或过长编码"),i);
        i+=n;
    }
    const auto text=QString::fromUtf8(b.constData(),b.size());
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
