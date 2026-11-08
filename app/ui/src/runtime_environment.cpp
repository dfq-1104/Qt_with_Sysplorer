#include "runtime_environment.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSet>
#include <QSettings>
#include <cstdlib>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

bool prepareRuntimeEnvironment(QString* error) {
    QFile binding(QCoreApplication::applicationDirPath()+"/local-worktree.json");
    auto fail=[&](const QString& detail){
        *error=QString::fromUtf8("私有运行环境准备失败：%1。请使用当前工作树的 app/ui/run_ui.bat，或重新准备运行包。").arg(detail);
        return false;
    };
    if(!binding.exists()){
        QSettings config(QCoreApplication::applicationDirPath()+"/sysplorer.ini",QSettings::IniFormat);
        if(config.value("Runtime/WorktreeBound",false).toBool())return fail(QString::fromUtf8("运行包缺少 local-worktree.json，请完整解压运行包"));
        return true;
    }
    if(!binding.open(QIODevice::ReadOnly))return fail(binding.errorString());
    const auto root=QJsonDocument::fromJson(binding.readAll()).object()["worktreeRoot"].toString();
    if(root.isEmpty()||!QFileInfo(root).isAbsolute())return fail(QString::fromUtf8("运行包绑定缺少有效的工作树路径"));
    const auto script=QDir(root).filePath("tools/deployment/Export-ClientEnvironment.ps1");
    if(!QFileInfo(script).isFile())return fail(QString::fromUtf8("找不到配套环境脚本：%1").arg(script));
    QProcess helper;
#ifdef Q_OS_WIN
    helper.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args){args->flags|=CREATE_NO_WINDOW;});
#endif
    helper.setProgram(qEnvironmentVariable("SystemRoot","C:/Windows")+"/System32/WindowsPowerShell/v1.0/powershell.exe");
    helper.setArguments({"-NoProfile","-NonInteractive","-ExecutionPolicy","Bypass","-File",script,"-ClientExecutable",QCoreApplication::applicationFilePath()});
    helper.setWorkingDirectory(root);helper.start();
    if(!helper.waitForStarted(5000))return fail(helper.errorString());
    if(!helper.waitForFinished(60000)){helper.kill();helper.waitForFinished();return fail(QString::fromUtf8("环境脚本执行超时"));}
    if(helper.exitStatus()!=QProcess::NormalExit||helper.exitCode()!=0)return fail(QString::fromUtf8(helper.readAllStandardError()).trimmed());
    const auto result=QJsonDocument::fromJson(helper.readAllStandardOutput()).object();
    if(result["schema"].toInt()!=1||QDir::cleanPath(result["worktreeRoot"].toString())!=QDir::cleanPath(root))return fail(QString::fromUtf8("环境脚本返回了不同的工作树绑定"));
    const auto environment=result["environment"].toObject();
    const QSet<QString> allowed={"SYSPLORER_HOME","BATTERY_SIM_WORKSPACE","BATTERY_DISCOVERY_DIR","BATTERY_REMOTE_CONFIG","TEMP","TMP","PATH","BATTERY_WORKTREE_ROOT"};
    for(const auto& name:allowed)if(!environment[name].isString()||environment[name].toString().isEmpty())return fail(QString::fromUtf8("环境字段缺失：%1").arg(name));
    for(const auto& name:allowed){
        const auto value=environment[name].toString();
#ifdef Q_OS_WIN
        if(_wputenv_s(reinterpret_cast<const wchar_t*>(name.utf16()),reinterpret_cast<const wchar_t*>(value.utf16()))!=0)return fail(QString::fromUtf8("无法设置环境字段：%1").arg(name));
#else
        if(!qputenv(name.toUtf8().constData(),value.toUtf8()))return fail(name);
#endif
    }
    return true;
}
