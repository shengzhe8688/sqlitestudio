#include <QString>
#include <QList>
#include <QStringList>
#include <QtTest>
#include "tsvserializer.h"
#include "csvserializer.h"

// TODO Add tests for CsvSerializer

class DsvFormatsTestTest : public QObject
{
        Q_OBJECT

    public:
        DsvFormatsTestTest();

private:
        QString toString(const QList<QStringList>& input);

        QList<QStringList> sampleData;
        QList<QStringList> sampleDeserializedData;
        QString sampleTsv;

    private Q_SLOTS:
        void initTestCase();
        void cleanupTestCase();
        void testTsv1();
        void testTsv2();
        void testCsv1();
        void testCsvPerformance();
};

DsvFormatsTestTest::DsvFormatsTestTest()
{
}

QString DsvFormatsTestTest::toString(const QList<QStringList>& input)
{
    QStringList outputLines;
    for (const QStringList& list : input)
        outputLines << "QStringList("+list.join(", ")+")";

    return "QList(\n    "+outputLines.join(",\n    ")+"\n)";
}

void DsvFormatsTestTest::initTestCase()
{
    sampleData << QStringList{"a", "b c", "\"d\""};
    sampleData << QStringList{"a\"a\"", "\"b\"c\"", "d\"\"e"};
    sampleData << QStringList{"a\na", "b\tc", "d\t\"e"};
    sampleData << QStringList{"a", "", "b", ""};

#ifdef Q_OS_MACX
    QString lineSep = "\r";
#else
    QString lineSep = "\n";
#endif

    sampleTsv = "";
    sampleTsv += "a\tb c\t\"d\"";
    sampleTsv += lineSep;
    sampleTsv += "a\"a\"\t\"b\"c\"\td\"\"e";
    sampleTsv += lineSep;
    sampleTsv += "\"a\na\"\t\"b\tc\"\t\"d\t\"\"e\"";
    sampleTsv += lineSep;
    sampleTsv += "a\t\tb\t";

    sampleDeserializedData << QStringList{"a", "b c", "\"d\""};
    sampleDeserializedData << QStringList{"a\"a\"", "\"b\"c\"", "d\"\"e"};
    sampleDeserializedData << QStringList{"a\na", "\"b", "c\"", "\"d", "\"\"e\""};
    sampleDeserializedData << QStringList{"a", "", "b", ""};
}

void DsvFormatsTestTest::cleanupTestCase()
{
}

void DsvFormatsTestTest::testTsv1()
{
    QString result = TsvSerializer::serialize(sampleData);

    QString common = "";
    int i;
    if (result != sampleTsv)
    {
        int lgt = qMax(result.length(), sampleTsv.length());
        for (i = 0; i < lgt && result[i] == sampleTsv[i]; i++)
            common.append(result[i]);
    }

    QVERIFY2(result == sampleTsv, QString("Mismatch after %1: %2\nSample: %3\nGot   : %4").arg(i).arg(common, sampleTsv, result).toLocal8Bit().data());
}

void DsvFormatsTestTest::testTsv2()
{
    QList<QStringList> result = TsvSerializer::deserialize(sampleTsv);

    QVERIFY2(result == sampleDeserializedData, QString("Sample: %1\nGot: %2").arg(toString(sampleDeserializedData), toString(result)).toLocal8Bit().data());
}

void DsvFormatsTestTest::testCsv1()
{
    QList<QStringList> result = CsvSerializer::deserialize(QString("a,\"\""), CsvFormat::DEFAULT);

    QVERIFY(result.size() == 1);
    QVERIFY(result.first().size() == 2);
}

void DsvFormatsTestTest::testCsvPerformance()
{
    QString input;
    for (int i = 0; i < 10000; i++)
        input += "abc,def,ghi,jkl,mno\n";

    QTemporaryFile theFile;
    theFile.open();
    theFile.write(input.toLatin1());
    theFile.seek(0);
    QTextStream stream(&theFile);

    QTime timer;
    timer.start();
    QList<QStringList> result = CsvSerializer::deserialize(stream, CsvFormat::DEFAULT);
    int time = timer.elapsed();

    QVERIFY(result.size() == 10000);
    QVERIFY(result.first().size() == 5);
    QVERIFY(result.last().size() == 5);

    qDebug() << "Deserialization time:" << time;
}

QTEST_APPLESS_MAIN(DsvFormatsTestTest)

#include "tst_dsvformatstesttest.moc"
