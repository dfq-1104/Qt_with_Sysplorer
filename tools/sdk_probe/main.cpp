#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QTextStream>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QCryptographicHash>
#include <QTextCodec>
#include <algorithm>
#include <windows.h>
#include <tlhelp32.h>
#include <cmath>
#include <rtcapi.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <clocale>
#include <mw_class_manager/mw_class_manager.h>
#include <mw_class_manager/mw_mo_handler.h>
#include <mw_class_manager/mw_core_option.h>
#include <mw_sim_inst/mw_sim_control.h>
#include <mw_sim_inst/mw_sim_data.h>
#include <mw_global/mw_setting.h>
#include <mw_utility/mw_runtime.h>
#include <modelica_post_processor/i_model_sim_instance.h>
#include <modelica_post_processor/i_model_simulation.h>
#include <mw_license_ui/mw_license_ui.h>

static void say(const QString& s) { QTextStream stream(stdout); stream.setCodec("UTF-8"); stream<<s<<endl; }
static void qtLog(QtMsgType type,const QMessageLogContext&,const QString& message) {
 fprintf(stderr,"QT[%d] %s\n",int(type),message.toUtf8().constData()); fflush(stderr);
}
// Diagnostic interception is restricted to this helper's own import tables.
static decltype(&CreateProcessW) realCreate = &CreateProcessW;
static BOOL WINAPI tracedCreate(LPCWSTR a, LPWSTR c, LPSECURITY_ATTRIBUTES p,
 LPSECURITY_ATTRIBUTES t, BOOL h, DWORD f, LPVOID e, LPCWSTR d,
 LPSTARTUPINFOW s, LPPROCESS_INFORMATION i) {
 say("PROCESS app="+QString::fromWCharArray(a ? a : L"")+" cmd="+
     QString::fromWCharArray(c ? c : L"")+" cwd="+QString::fromWCharArray(d ? d : L""));
 BOOL ok=realCreate(a,c,p,t,h,f,e,d,s,i); DWORD err=GetLastError();
 say(QString("PROCESS result=%1 win32=%2 pid=%3").arg(ok).arg(ok ? 0 : err).arg(ok ? i->dwProcessId : 0));
 SetLastError(err); return ok;
}
static void traceProcesses() {
 realCreate=(decltype(realCreate))GetProcAddress(GetModuleHandleW(L"KernelBase.dll"),"CreateProcessW");
 HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,GetCurrentProcessId());
 MODULEENTRY32 m={}; m.dwSize=sizeof(m);
 if(Module32First(snap,&m)) do {
  if(!QString::fromWCharArray(m.szExePath).contains("MWORKS",Qt::CaseInsensitive)) continue;
  auto base=(BYTE*)m.modBaseAddr;
  auto nt=(IMAGE_NT_HEADERS*)(base+((IMAGE_DOS_HEADER*)base)->e_lfanew);
  auto rva=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
  if(!rva) continue;
  auto desc=(IMAGE_IMPORT_DESCRIPTOR*)(base+rva);
  for(;desc->Name;++desc) {
   if(!desc->OriginalFirstThunk) continue;
   auto names=(IMAGE_THUNK_DATA*)(base+desc->OriginalFirstThunk);
   auto thunks=(IMAGE_THUNK_DATA*)(base+desc->FirstThunk);
   for(;names->u1.AddressOfData;++names,++thunks) {
    if(IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
    auto imp=(IMAGE_IMPORT_BY_NAME*)(base+names->u1.AddressOfData);
    if(strcmp((char*)imp->Name,"CreateProcessW")) continue;
    DWORD old=0;
    if(VirtualProtect(&thunks->u1.Function,sizeof(ULONG_PTR),PAGE_READWRITE,&old)) {
     thunks->u1.Function=(ULONG_PTR)&tracedCreate;
     DWORD ignored; VirtualProtect(&thunks->u1.Function,sizeof(ULONG_PTR),old,&ignored);
    }
   }
  }
 } while(Module32Next(snap,&m));
 CloseHandle(snap);
}
class Logger: public ILoggingReceiver {
 void ShowLogInfo(MWK::LogInfo* p) override {
  if(!p) return;
  QString location;
  if(!p->m_strFile.empty()) {
   location=" ["+QString::fromUtf8(p->m_strFile.c_str());
   if(p->m_uRow>0&&p->m_uRow<1000000&&p->m_uColumn<1000000)
    location+=QString(":%1:%2").arg(p->m_uRow).arg(p->m_uColumn);
   location+="]";
  }
  say(QString("SDK #%1%2 %3").arg(p->m_uNumber).arg(location).arg(QString::fromUtf8(p->m_strText.c_str())));
 }
};
static int rtcReport(int, const char*, int, const char*, const char* format, ...) {
 char text[4096]; va_list ap; va_start(ap,format); vsnprintf(text,sizeof(text),format,ap); va_end(ap);
 fprintf(stderr,"RTC: %s\n",text); fflush(stderr); return 0;
}
static int runProbe() {
 const auto args=QCoreApplication::arguments();
 if(args.size()<5) { say("usage: sdk_probe model.mo output_dir soc0 current [vc]"); return 2; }
 QString mo=QFileInfo(args[1]).absoluteFilePath();
 QString out=QFileInfo(args[2]).absoluteFilePath();
 double soc0=args[3].toDouble(), current=args[4].toDouble();
 const bool custom=args.contains("--mode");
 auto option=[&](const QString& key, const QString& fallback) {
  int index=args.indexOf(key); return index>=0 && index+1<args.size()?args[index+1]:fallback;
 };
 int mode=option("--mode","2").toInt();
 double voltage=option("--voltage",mode==1?"3.45":"2.5").toDouble();
 double duration=option("--duration","600").toDouble();
 if(!(std::isfinite(soc0)&&soc0>=0 && soc0<=1 && std::isfinite(current)&&current>=0 && current<=5
    && (mode==1||mode==2) && std::isfinite(voltage)&&voltage>=2.5&&voltage<=3.45
    && std::isfinite(duration)&&duration>0&&duration<=20000)) {say("FAIL invalid request"); return 3;}
 bool existing=args.contains("existing");
 if(!existing && QFile::exists(out+"/momodel.c")) { say("FAIL: output directory must be fresh"); return 4; }
 QDir().mkpath(out);
 QFile executable(QCoreApplication::applicationFilePath()); executable.open(QIODevice::ReadOnly);
 wchar_t qtModule[MAX_PATH]={}; GetModuleFileNameW(GetModuleHandleW(L"Qt5Core.dll"),qtModule,MAX_PATH);
 say("HELPER exe="+QCoreApplication::applicationFilePath()+" sha256="+
     QString::fromLatin1(QCryptographicHash::hash(executable.readAll(),QCryptographicHash::Sha256).toHex())+
     " qt="+QString::fromWCharArray(qtModule)+" model="+mo);
 // Keep the original bytes for provenance; give the SDK a separately committed,
 // explicitly marked UTF-8 file instead of relying on BOM-less encoding detection.
 const bool piped=args.contains("--model-stdin");
 QByteArray originalBytes;
 if(piped) {
  QFile pipe;
  if(!pipe.open(stdin,QIODevice::ReadOnly)) {say("FAIL model stdin unavailable");return 11;}
  const auto payload=pipe.readAll();
  QJsonParseError parseError;
  const auto request=QJsonDocument::fromJson(payload,&parseError).object();
  originalBytes=QByteArray::fromBase64(request.value("modelBase64").toString().toLatin1());
  const auto actual=QString::fromLatin1(QCryptographicHash::hash(originalBytes,QCryptographicHash::Sha256).toHex());
  if(payload.size()>2*1024*1024||originalBytes.isEmpty()||originalBytes.size()>1024*1024
     ||parseError.error!=QJsonParseError::NoError||request.value("schema").toInt()!=1
     ||actual!=request.value("sha256").toString()||actual!=option("--model-sha256","")) {
   say("FAIL model pipe integrity check");return 11;
  }
  say("MODEL transport=stdin integrity=verified sha256="+actual);
 } else {
  QFile original(mo);
  if(!original.open(QIODevice::ReadOnly)) {say("FAIL model file is unreadable: "+mo);return 11;}
  originalBytes=original.readAll();
  if(original.error()!=QFileDevice::NoError) {say("FAIL model read error: "+original.errorString());return 11;}
 }
 QTextCodec::ConverterState decoding;
 QByteArray body=originalBytes;
 const char* codec="UTF-8";
 if(body.startsWith(QByteArray::fromHex("fffe"))) {codec="UTF-16LE";body.remove(0,2);}
 else if(body.startsWith(QByteArray::fromHex("feff"))) {codec="UTF-16BE";body.remove(0,2);}
 else if(body.startsWith(QByteArray::fromHex("efbbbf"))) body.remove(0,3);
 const auto modelText=QTextCodec::codecForName(codec)->toUnicode(body.constData(),body.size(),&decoding);
 say(QString("DECODE bytes=%1 sha256=%2 codec=%3 invalid=%4 remaining=%5 null=%6 chars=%7")
     .arg(originalBytes.size()).arg(QString::fromLatin1(QCryptographicHash::hash(originalBytes,QCryptographicHash::Sha256).toHex()))
     .arg(codec).arg(decoding.invalidChars).arg(decoding.remainingChars).arg(modelText.contains(QChar(0))).arg(modelText.size()));
 if(decoding.invalidChars||decoding.remainingChars||modelText.contains(QChar(0))||modelText.trimmed().isEmpty()) {
  const int winChars=QString::fromLatin1(codec)=="UTF-8"?
      MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,body.constData(),body.size(),nullptr,0):-1;
  say(QString("FAIL model decode rejected: invalid=%1 remaining=%2 null=%3 empty=%4 winUtf8Chars=%5 head=%6 tail=%7 file=%8")
      .arg(decoding.invalidChars).arg(decoding.remainingChars).arg(modelText.contains(QChar(0)))
      .arg(modelText.trimmed().isEmpty()).arg(winChars).arg(QString::fromLatin1(originalBytes.left(32).toHex()))
      .arg(QString::fromLatin1(originalBytes.right(32).toHex())).arg(mo));return 11;
 }
 const QByteArray sdkBytes=(piped?QByteArray():QByteArray::fromHex("efbbbf"))+modelText.toUtf8();
 const QString inputDir=out+"/sdk_input";
 if(!QDir().mkpath(inputDir)) {say("FAIL cannot create SDK input directory");return 11;}
 const QString sdkModel=inputDir+"/BatterySim.mo";
 if(!piped) {
 QSaveFile input(sdkModel);
 if(!input.open(QIODevice::WriteOnly)||input.write(sdkBytes)!=sdkBytes.size()||!input.commit()) {
  say("FAIL cannot commit SDK model copy");return 11;
 }
 QFile check(sdkModel);
 if(!check.open(QIODevice::ReadOnly)||check.readAll()!=sdkBytes) {say("FAIL SDK model copy verification");return 11;}
 check.close();
 }
 const auto sourceHash=QString::fromLatin1(QCryptographicHash::hash(originalBytes,QCryptographicHash::Sha256).toHex());
 const auto sdkHash=QString::fromLatin1(QCryptographicHash::hash(sdkBytes,QCryptographicHash::Sha256).toHex());
 say("MODEL sourceSha256="+sourceHash+" sdkSha256="+sdkHash+" encoding="+(piped?"UTF-8":"UTF-8-BOM"));
 if(args.contains("--check-model")) return 0;
 say("Qt="+QString(qVersion())+" exe="+QCoreApplication::applicationFilePath());
 say(QString("ENCODING ACP=%1 console=%2 output=%3 locale=%4").arg(GetACP()).arg(GetConsoleCP())
     .arg(GetConsoleOutputCP()).arg(QString::fromLatin1(std::setlocale(LC_ALL,nullptr))));
 say("INSTALL="+QString::fromWCharArray(MwRunTime::GetInstallPath())+" BIN="+QString::fromWCharArray(MwRunTime::GetBinPath()));
 // These overrides live in memory; do not save or modify the installed settings.
 if(args.contains("vc")) {
  MwSetting::SetStringValue("MWorks.Simulation.CCompiler.Type","vc");
  MwSetting::SetStringValue("MWorks.Simulation.CCompiler.Name","Microsoft Visual Studio 2017");
  MwSetting::SetBoolValue("MWorks.Simulation.CCompiler.CustomCompilerPlatformX64",true);
  MwSetting::SetStringValue("MWorks.Simulation.CCompiler.PathX64",
   "C:/Program Files (x86)/Microsoft Visual Studio/2017/Professional/VC");
 }
 say("setting compiler="+QString::fromStdString(MwSetting::GetStringValue("MWorks.Simulation.CCompiler.Type")));
 MwClassManager* manager=nullptr; MwCoreOption* corePtr=nullptr;
 if(!existing) {
 manager = new MwClassManager; auto& mgr=*manager;
 mgr.Initialize();
 corePtr=new MwCoreOption(&mgr); auto& core=*corePtr;
 say(QString("CORE ownership registered=%1").arg(mgr.GetCoreOption()==corePtr));
 if(args.contains("vc")) {
  const std::string vc="C:/Program Files (x86)/Microsoft Visual Studio/2017/Professional/VC";
  core.SetCompileInfo("vc","Microsoft Visual Studio 2017",vc,vc);
  core.SetCompilePlatform(PF_WIN64);
 }
 std::string ct,cn,c32,c64; core.GetCompileInfo(&ct,&cn,&c32,&c64);
 say(QString("CORE compiler=%1 name=%2 x86=%3 x64=%4 platform=%5")
  .arg(QString::fromStdString(ct)).arg(QString::fromStdString(cn))
  .arg(QString::fromStdString(c32)).arg(QString::fromStdString(c64)).arg((int)core.CompilePlatform()));
 auto handler=mgr.GetMoHandler(); if(!handler) return 10;
 Logger logger;
 say(QString("LoadMSL=%1").arg(handler->LoadMoLibrary("Modelica","4.0",&logger)));
 const bool modelLoaded=piped?bool(handler->NewModel(modelText.toUtf8().toStdString(),MoKey())):
     handler->OpenFile(sdkModel.toUtf8().toStdString(),&logger);
 say(QString("MODEL load=%1 method=%2").arg(modelLoaded).arg(piped?"NewModel(text)":"OpenFile"));
 if(!modelLoaded) {
  say("FAIL OpenFile: "+sdkModel+" sourceSha256="+sourceHash+" sdkSha256="+sdkHash);return 11;
 }
 auto key=handler->DirectLookupType(custom?"BatterySim.SingleRun":"BatterySim.BatteryDemo"); if(!key) return 12;
 auto set=[&](const MwStrVector& path,double value) {
  const auto text=QString::number(value,'g',15).toStdString();
  bool ok=handler->SetParamValue(key,path,text);
  const auto back=QString::fromStdString(handler->GetParamValue(key,path));
  say("PARAM "+QString::fromStdString(path.back())+"="+back);
  return ok&&std::abs(back.toDouble()-value)<1e-10;
 };
 bool a=set(custom?MwStrVector{"socStart"}:MwStrVector{"cell","SOC0"},soc0);
 bool b=set(custom?MwStrVector{"runCurrent"}:MwStrVector{"cell","I_set"},current);
 if(custom && (!set({"runMode"},mode)||!set({"runVoltage"},voltage))) return 13;
 say(QString("PARAM verified=%1/%2 requested SOC0=%3 I_set=%4").arg(a).arg(b).arg(soc0).arg(current));
 if(!a||!b) return 13;
 if(args.contains("--trace")) traceProcesses();
 Status status={};
 bool compiled=handler->CompileModel(key,out.toStdString(),&logger,&status);
 say(QString("COMPILE=%1 status=%2 dll=%3").arg(compiled).arg((int)status).arg(QFile::exists(out+"/MWSolver.dll")));
 if(!compiled) return 20;
 }
 auto dataPtr=new MwSimData(L"BatteryDemo",(out+"/Result.msr").toStdWString(),out.toStdWString()); auto& data=*dataPtr;
 if(!data.InitializeSimInst()) { say("FAIL InitializeSimInst"); return 21; }
 MwExperimentData exp; exp.startTime=0; exp.stopTime=duration;
 exp.intervalLength=1; exp.numberOfIntervals=int(std::ceil(duration)); data.SetExperimentData(exp);
 auto ctrlPtr=new MwSimControl(&data); auto& ctrl=*ctrlPtr; ctrl.SetLogDebugLevel(true);
 auto inst=data.GetSimInstance();
 if(!inst->SimControl()) {
  if(!inst->CreateSimControl(ctrl.GetSimControlOptions().c_str())) { say("FAIL CreateSimControl"); return 27; }
 }
 inst->SimControl()->SetTargetPlatform(PF_WIN64);
 QEventLoop loop; bool done=false; int error=-1; double end=-1;
 QObject::connect(&data,&MwSimData::SigPostSimStopped,&loop,[&](double t,int err){
  done=true; error=err; end=t; loop.quit(); });
 QTimer timer; timer.setSingleShot(true);
 QObject::connect(&timer,&QTimer::timeout,&loop,[&](){ctrl.KillSimulate();loop.quit();});
 timer.start(120000);
 if(!ctrl.StartSimulate()) { say("FAIL StartSimulate"); return 22; }
 if(!done) loop.exec();
 say(QString("STOP done=%1 error=%2 t=%3").arg(done).arg(error).arg(end));
 if(!done||error!=0||std::abs(end-duration)>0.01) return 23;
 std::vector<double> ts,ys,ti,is,tv,vs;
 if(!data.GetVarData("cell.SOC",ts,ys)||!data.GetVarData("cell.I_act",ti,is)||
    !data.GetVarData("cell.V_term",tv,vs)||ys.empty()||is.empty()||vs.empty()) return 24;
 bool pass=ts.size()==ys.size()&&ti.size()==is.size()&&tv.size()==vs.size()
   &&std::abs(ts.front())<1e-6 && std::abs(ts.back()-duration)<0.01
   &&std::abs(ys.front()-soc0)<1e-6;
 if(!custom) pass=pass&&std::abs(is.front()+current)<1e-6
   &&std::abs(ys.back()-(soc0-current*duration/9000))<1e-4;
 for(size_t j=0;j<ys.size();++j) pass=pass&&std::isfinite(ts[j])&&std::isfinite(ys[j])
   &&ys[j]>=-1e-5&&ys[j]<=1.00001&&(j==0||ts[j]>=ts[j-1]);
 for(double i:is) pass=pass&&std::isfinite(i)&&std::abs(i)<=current+1e-4
   &&(mode==1?i>=-1e-5:i<=1e-5);
 for(double v:vs) pass=pass&&std::isfinite(v);
 if(!pass) {say("FAIL result validation"); return 26;}
 QFile file(out+"/result.csv"); if(!file.open(QIODevice::WriteOnly|QIODevice::Text)) return 25;
 QTextStream csv(&file); csv.setRealNumberPrecision(15); csv<<"time,SOC,I_act,V_term\n";
 auto valueAt=[](const std::vector<double>& times,const std::vector<double>& values,double t) {
  auto upper=std::upper_bound(times.begin(),times.end(),t);
  if(upper==times.begin()) return values.front();
  if(upper==times.end()) return values.back();
  size_t hi=upper-times.begin(),lo=hi-1;
  return values[lo]+(values[hi]-values[lo])*(t-times[lo])/(times[hi]-times[lo]);
 };
 for(size_t i=0;i<ys.size();++i) csv<<ts[i]<<","<<ys[i]<<","<<valueAt(ti,is,ts[i])<<","<<valueAt(tv,vs,ts[i])<<"\n";
 say(QString("RESULT %1 SOC0=%2 I0=%3 SOC600=%4 voltage0=%5 points=%6")
  .arg(pass?"PASS":"FAIL").arg(ys.front(),0,'g',15).arg(is.front()).arg(ys.back(),0,'g',15).arg(vs.front()).arg(ys.size()));
 csv.flush(); file.close(); timer.stop();
 QJsonObject result{{"schema",1},{"source","sysplorer-sdk"},{"model",custom?"BatterySim.SingleRun":"BatterySim.BatteryDemo"},
  {"modelSha256",sourceHash},{"sdkInputSha256",sdkHash},{"sdkInputEncoding",piped?"UTF-8":"UTF-8-BOM"},
  {"modelTransport",piped?"stdin":"file"},{"sdkLoadMethod",piped?"NewModel(text)":"OpenFile"},
  {"mode",mode},{"soc0",soc0},{"current",current},{"voltage",voltage},{"duration",duration},
  {"initialSOC",ys.front()},{"initialCurrent",is.front()},{"finalSOC",ys.back()},{"points",int(ys.size())}};
 QSaveFile manifest(out+"/result.json");
 if(!manifest.open(QIODevice::WriteOnly)||manifest.write(QJsonDocument(result).toJson())<0||!manifest.commit()) return 29;
 say("CLEANUP control"); ctrl.RebindSimData(nullptr); delete ctrlPtr;
 say("CLEANUP data"); data.CloseResult(); delete dataPtr;
 say("CLEANUP core");
 if(manager && manager->GetCoreOption()!=corePtr) delete corePtr;
 say("CLEANUP manager"); delete manager;
 say("CLEANUP done"); return pass?0:26;
}
int main(int argc,char** argv) {
 _RTC_SetErrorFunc(rtcReport);
 qInstallMessageHandler(qtLog);
 auto app=new QApplication(argc,argv);
 int code=runProbe();
 say("CLEANUP probe stack done");
 auto collector=MwLicenseActivityCollector::Instance();
 collector->quit();
 bool collectorStopped=collector->wait(10000);
 auto queue=MwLicenseUI::GetQueueActivateThead();
 bool queueStopped=true;
 if(queue && queue->isRunning()) { queue->quit(); queueStopped=queue->wait(10000); }
 say(QString("CLEANUP license threads collector=%1 queue=%2").arg(collectorStopped).arg(queueStopped));
 if(!collectorStopped||!queueStopped) code=28;
 delete app;
 say("CLEANUP application done");
 qInstallMessageHandler(qtLog);
 return code;
}


