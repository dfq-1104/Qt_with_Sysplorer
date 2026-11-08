#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUdpSocket>
#include <QNetworkDatagram>
#include <QDir>
#include <QSaveFile>
#include <QDateTime>
#include <QUuid>
#include <QTimer>
#include <QElapsedTimer>
#include <QProcess>
#include "../../../common/model_transfer.h"
#ifdef Q_OS_WIN
#include <windows.h>
#endif

// Process-launch recorder only; never substitutes for solver validation.
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);
    QFile marker(QString::fromUtf8(QByteArray::fromBase64(qgetenv("BATTERY_LAUNCH_SENTINEL"))));
    if(!marker.open(QIODevice::WriteOnly|QIODevice::Append))return 2;
    QJsonObject record{{"pid",double(QCoreApplication::applicationPid())},{"arguments",QJsonArray::fromStringList(app.arguments())}};
    if(qEnvironmentVariableIsSet("BATTERY_FIXTURE_SOLVER")){
        record["role"]=app.arguments().contains("--server-runtime-child")?"solver":"host";
        record["path"]=qEnvironmentVariable("PATH");record["workspace"]=qEnvironmentVariable("BATTERY_SIM_WORKSPACE");
        record["workingDirectory"]=QDir::currentPath();
        for(const auto name:{"QT_QPA_PLATFORM","QT_QPA_PLATFORM_PLUGIN_PATH","QT_PLUGIN_PATH","PYTHONHOME","PYTHONPATH"})record[name]=qEnvironmentVariable(name);
    }
    marker.write(QJsonDocument(record).toJson(QJsonDocument::Compact));marker.write("\n");marker.flush();
    if(qEnvironmentVariableIsSet("BATTERY_FIXTURE_SOLVER")){
        marker.close();
        if(app.arguments().contains("--server-runtime-child"))return 0;
#ifdef Q_OS_WIN
        SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
#endif
        // The child needs Qt5Core.dll, placed only in the fixture host Bin64.
        // Thus this exercises actual descendant DLL loading, not string matching.
        QProcess child;child.setProgram(QDir(app.applicationDirPath()).absoluteFilePath("../Simulator/Bin64/MWSolver.exe"));
        child.setArguments({"--server-runtime-child"});child.start();
        if(!child.waitForStarted()||!child.waitForFinished(10000)){child.kill();child.waitForFinished();return 4;}
        return child.exitCode();
    }
    if(!qEnvironmentVariableIsSet("BATTERY_FIXTURE_UDP"))return 0;
    QUdpSocket socket;if(!socket.bind(QHostAddress(QHostAddress::LocalHost),quint16(0)))return 3;
    const auto session=QUuid::createUuid().toString(QUuid::WithoutBraces),token=session+session;
    const auto folder=qEnvironmentVariable("BATTERY_DISCOVERY_DIR");QDir().mkpath(folder);
    const auto endpoint=folder+"/"+QString::number(app.applicationPid())+".json";
    QElapsedTimer elapsed;elapsed.start();
    auto advertise=[&]{QSaveFile f(endpoint);if(f.open(QIODevice::WriteOnly)){f.write(QJsonDocument(QJsonObject{{"schema",1},{"protocol","battery-udp/1"},
        {"pid",double(app.applicationPid())},{"executable",app.applicationFilePath()},{"port",socket.localPort()},{"session",session},{"token",token},
        {"modelHash",qEnvironmentVariable("BATTERY_FIXTURE_MODEL_HASH")},{"bound",true},{"updated",QDateTime::currentDateTimeUtc().addSecs(elapsed.elapsed()<qEnvironmentVariableIntValue("BATTERY_FIXTURE_STALE_UNTIL_MS")?-30:0).toString(Qt::ISODateWithMs)}}).toJson());f.commit();}};
    QTimer timer;QObject::connect(&timer,&QTimer::timeout,&app,advertise);timer.start(200);advertise();
    ModelTransfer::Store uploads;QString task,owner,model,phase="Ready";QJsonObject params;bool lostRequest=false,lostReply=false;
    QObject::connect(&socket,&QUdpSocket::readyRead,&app,[&]{while(socket.hasPendingDatagrams()){
        const auto d=socket.receiveDatagram();const auto r=QJsonDocument::fromJson(d.data()).object();
        if(r["token"].toString()!=token)continue;
        marker.write(QJsonDocument(QJsonObject{{"command",r["command"]}}).toJson(QJsonDocument::Compact));marker.write("\n");marker.flush();
        const bool ready=elapsed.elapsed()>=qEnvironmentVariableIntValue("BATTERY_FIXTURE_READY_AFTER_MS");
        QJsonObject reply{{"schema",1},{"id",r["id"]},{"taskId",r["taskId"]},{"session",session},{"command",r["command"]},{"ok",true},{"ready",ready},
            {"modelHash",qEnvironmentVariable("BATTERY_FIXTURE_MODEL_HASH")},{"controllerState",qEnvironmentVariableIsSet("BATTERY_FIXTURE_BUSY")?"Running":"Idle"},
            {"status",QJsonObject{{"phase","Ready"}}}};
        if(!qEnvironmentVariableIsSet("BATTERY_FIXTURE_MISSING_MODEL"))reply["modelLifecycle"]=true;
        if(!qEnvironmentVariableIsSet("BATTERY_FIXTURE_MISSING_SPLIT"))reply["splitLifecycle"]=true;
        if(!qEnvironmentVariableIsSet("BATTERY_FIXTURE_MISSING_TRANSFER"))reply["modelTransferVersion"]=1;reply["modelInterface"]="battery-console/1";
        if(qEnvironmentVariableIsSet("BATTERY_FIXTURE_TRANSFER")){
            const auto cmd=r["command"].toString();
            if(cmd=="model.chunk"&&!lostRequest){lostRequest=true;continue;}
            try{
                if(cmd.startsWith("model.")){
                    auto result=uploads.execute(r["client"].toString(),cmd,r["args"].toObject(),ModelTransfer::UdpChunk,elapsed.elapsed());
                    for(auto it=result.begin();it!=result.end();++it)reply[it.key()]=it.value();
                    if(cmd=="model.chunk"&&!lostReply){lostReply=true;continue;}
                }else if(cmd=="load"||cmd=="translate"){
                    params=r["args"].toObject();model=ModelTransfer::sha(uploads.take(r["client"].toString(),params["modelHash"].toString(),elapsed.elapsed()));
                    task=r["taskId"].toString();owner=r["client"].toString();phase=cmd=="load"?"Loaded":"Prepared";
                    marker.write(QJsonDocument(QJsonObject{{"acceptedHash",model}}).toJson(QJsonDocument::Compact));marker.write("\n");marker.flush();
                }else if(cmd=="discard")phase="Ready";
                else if(cmd=="start")phase="Running";
                else if(cmd=="stop")phase="Stopped";
                reply["status"]=QJsonObject{{"phase",phase},{"taskId",task},{"owner",owner},{"modelHash",model},{"params",params},{"modelLoaded",!model.isEmpty()}};
            }catch(const std::exception& e){reply["ok"]=false;reply["code"]=QString::fromUtf8(e.what());}
        }
        socket.writeDatagram(QJsonDocument(reply).toJson(QJsonDocument::Compact),d.senderAddress(),d.senderPort());
    }});
    QTimer::singleShot(20000,&app,&QCoreApplication::quit);const int result=app.exec();QFile::remove(endpoint);return result;
}
