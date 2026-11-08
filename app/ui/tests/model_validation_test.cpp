#include <QtTest>
#include <QTemporaryDir>
#include <QTextCodec>
#include "model_file.h"
class ValidationTest: public QObject {
    Q_OBJECT
    QByteArray original;
private slots:
    void initTestCase(){QFile f(qEnvironmentVariable("BATTERY_MODEL_FILE"));QVERIFY(f.open(QIODevice::ReadOnly));original=f.readAll();QVERIFY(original.size()>10000);}
    void bytes_data(){
        QTest::addColumn<QByteArray>("bytes");QTest::addColumn<bool>("ok");QTest::addColumn<int>("offset");
        const auto valid=QByteArray("package BatterySim model SingleRun end SingleRun; end BatterySim;\r\n");
        QTest::newRow("ascii-crlf")<<valid<<true<<-1;
        QTest::newRow("chinese")<<valid+QString::fromUtf8("// 中文模型 😀\r\n").toUtf8()<<true<<-1;
        QTest::newRow("bom")<<QByteArray::fromHex("efbbbf")+valid<<true<<-1;
        QTest::newRow("gbk")<<valid+QTextCodec::codecForName("GBK")->fromUnicode(QString::fromUtf8("中文"))<<false<<valid.size()+1;
        QTest::newRow("utf16")<<QByteArray::fromHex("fffe70006100")<<false<<0;
        QTest::newRow("nul")<<valid+QByteArray(1,0)<<false<<valid.size();
        QTest::newRow("truncated")<<valid+QByteArray::fromHex("e4b8")<<false<<valid.size();
        QTest::newRow("continuation")<<valid+QByteArray::fromHex("80")<<false<<valid.size();
        QTest::newRow("surrogate")<<valid+QByteArray::fromHex("eda080")<<false<<valid.size();
        QTest::newRow("overlong")<<valid+QByteArray::fromHex("e08080")<<false<<valid.size();
        QTest::newRow("too-high")<<valid+QByteArray::fromHex("f4908080")<<false<<valid.size();
        QTest::newRow("maximum-codepoint")<<valid+QByteArray::fromHex("f48fbfbf")<<true<<-1;
    }
    void bytes(){QFETCH(QByteArray,bytes);QFETCH(bool,ok);QFETCH(int,offset);bool accepted=true;int actual=-1;try{ModelTransfer::validate(bytes);}catch(const ModelTransfer::ValidationError& e){accepted=false;actual=e.offset;}QCOMPARE(accepted,ok);QCOMPARE(actual,offset);}
    void repeatedOriginal(){for(int i=0;i<10000;++i)ModelTransfer::validate(original);}
    void embeddedModel(){
        QCOMPARE(ModelFile::defaultPath(),QString(":/models/BatterySim.mo"));
        const auto r=ModelFile::read(ModelFile::defaultPath(),qEnvironmentVariable("BATTERY_TEST_ROOT")+"/embedded-diagnostics");
        QVERIFY2(r.ok,qPrintable(r.error));QCOMPARE(r.bytes,original);
        QCOMPARE(r.record["source"].toString(),QString("embedded-resource"));
    }
    void fileDiagnostics(){
        QTemporaryDir dir(qEnvironmentVariable("BATTERY_TEST_ROOT")+"/validation-XXXXXX");QVERIFY(dir.isValid());dir.setAutoRemove(false);
        const auto path=dir.path()+QString::fromUtf8("/中文 空格.mo");QFile f(path);QVERIFY(f.open(QIODevice::WriteOnly));f.write(original);f.close();
        for(int i=0;i<25;++i){const auto r=ModelFile::read(path,dir.path()+"/diagnostics");QVERIFY2(r.ok,qPrintable(r.error));QCOMPARE(r.bytes,original);QCOMPARE(r.record["sha256"].toString(),ModelTransfer::sha(original));QCOMPARE(r.record["bytesRead"].toInt(),original.size());}
        auto missing=ModelFile::read(dir.path()+"/missing.mo",dir.path()+"/diagnostics");QVERIFY(!missing.ok);QVERIFY(QFile::exists(missing.diagnostic));QCOMPARE(missing.record["code"].toString(),QString("MODEL_READ_FAILED"));
        QVERIFY(f.open(QIODevice::WriteOnly));f.write(original+QByteArray::fromHex("e4b8"));f.close();
        const auto bad=ModelFile::read(path,dir.path()+"/diagnostics");QVERIFY(!bad.ok);QCOMPARE(bad.record["byteOffset"].toInt(),original.size());QVERIFY(!bad.record.contains("bytes"));
        QVERIFY(f.open(QIODevice::ReadOnly));QCOMPARE(f.readAll(),original+QByteArray::fromHex("e4b8"));
        const auto directory=ModelFile::read(dir.path(),dir.path()+"/diagnostics");QVERIFY(!directory.ok);
        const auto denied=ModelFile::read(path,path+"/cannot-create");QVERIFY(!denied.ok);
    }
};
QTEST_GUILESS_MAIN(ValidationTest)
#include "model_validation_test.moc"
