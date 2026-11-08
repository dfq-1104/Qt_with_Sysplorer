#pragma once
#include "model_transfer.h"
#include "release_id.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QUuid>
#include <QJsonArray>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#ifndef BATTERY_RELEASE_ID
#define BATTERY_RELEASE_ID "development"
#endif
namespace ModelFile {
inline QString embeddedPath(){return QStringLiteral(":/models/BatterySim.mo");}
inline QString defaultPath(){
#ifdef BATTERY_MODEL_TEST_OVERRIDES
    // Fixture-only override. This macro is never enabled in the client build.
    return qEnvironmentVariable("BATTERY_MODEL_FILE",embeddedPath());
#else
    return embeddedPath();
#endif
}
struct ReadResult {QByteArray bytes;QString error,diagnostic;QJsonObject record;bool ok=false;};
inline ReadResult read(const QString& path,const QString& diagnosticDirectory){
    ReadResult r;QDir().mkpath(diagnosticDirectory);
    r.diagnostic=QDir(diagnosticDirectory).absoluteFilePath("model-"+QUuid::createUuid().toString(QUuid::WithoutBraces)+".json");
    r.record={{"releaseId",BATTERY_RELEASE_ID},{"executable",QCoreApplication::applicationFilePath()},
        {"pid",double(QCoreApplication::applicationPid())},{"path",QFileInfo(path).absoluteFilePath()},
        {"source",path.startsWith(":/")?"embedded-resource":"test-file"},
        {"utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},{"stage","before-read"}};
    QJsonArray accessModules;
#ifdef Q_OS_WIN
    for(const auto name:{L"TyEnc64.dll",L"intefsctrl64.dll"})if(GetModuleHandleW(name))accessModules.append(QString::fromWCharArray(name));
#endif
    r.record["fileAccessModules"]=accessModules;
    auto save=[&]{QSaveFile f(r.diagnostic);if(!f.open(QIODevice::WriteOnly)||f.write(QJsonDocument(r.record).toJson())<0||!f.commit())return false;return true;};
    if(!save()){r.error=QString::fromUtf8("无法创建模型诊断文件：%1").arg(r.diagnostic);return r;}
    QFile f(path);QFileInfo before(path);
    r.record["sizeBefore"]=double(before.size());
    r.record["modifiedBefore"]=before.lastModified().toUTC().toString(Qt::ISODateWithMs);
    QString code,reason;int offset=-1;
    if(!f.open(QIODevice::ReadOnly)){code="MODEL_READ_FAILED";reason=f.errorString();}
    else if(f.size()>ModelTransfer::Limit){code="MODEL_SIZE_INVALID";reason=QString::fromUtf8("模型超过 1 MiB");}
    else{
        r.bytes=f.read(ModelTransfer::Limit+1);const auto readError=f.error();f.close();QFileInfo after(path);
        r.record["sizeAfter"]=double(after.size());r.record["bytesRead"]=r.bytes.size();
        r.record["modifiedAfter"]=after.lastModified().toUTC().toString(Qt::ISODateWithMs);
        r.record["sha256"]=ModelTransfer::sha(r.bytes);
        // A bounded binary signature distinguishes a filtered/encrypted view
        // from the normal package without recording model text.
        r.record["leading8BytesHex"]=QString::fromLatin1(r.bytes.left(8).toHex());
        if(readError!=QFileDevice::NoError||r.bytes.size()!=before.size()||after.size()!=before.size()||after.lastModified()!=before.lastModified()){
            code="MODEL_READ_INCOMPLETE";reason=QString::fromUtf8("模型读取不完整或读取期间文件发生变化，请重试");
        }else try{ModelTransfer::validate(r.bytes);}
        catch(const ModelTransfer::ValidationError& e){code=QString::fromLatin1(e.what());reason=e.reason;offset=e.offset;}
        catch(const std::exception& e){code=QString::fromLatin1(e.what());reason=QString::fromUtf8("未找到 BatterySim.SingleRun 模型接口");}
    }
    r.ok=code.isEmpty();r.record["ok"]=r.ok;r.record["stage"]="complete";r.record["code"]=code;r.record["reason"]=reason;r.record["byteOffset"]=offset;
    if(!code.isEmpty())r.error=QString::fromUtf8("%1：%2%3\n模型：%4\n诊断：%5").arg(code,reason,offset<0?QString():QString::fromUtf8("，字节位置 %1（从 0 开始）").arg(offset),QFileInfo(path).absoluteFilePath(),r.diagnostic);
    if(code=="MODEL_UTF8_INVALID"&&!path.startsWith(":/")&&!accessModules.isEmpty()){
        const auto hint=QString::fromUtf8("当前进程检测到文件加密模块，读取到 %1 字节。若独立检查的长度或哈希不同，请由管理员核查并批准此客户端读取模型明文的权限。请勿反复转换编码或停用安全软件。").arg(r.bytes.size());
        r.record["fileAccessHint"]=hint;r.error+="\n"+hint;
    }
    if(!save()){r.ok=false;r.error=QString::fromUtf8("无法完成模型诊断记录：%1").arg(r.diagnostic);}
    return r;
}
}
