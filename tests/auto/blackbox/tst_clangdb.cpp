/****************************************************************************
**
** Copyright (C) 2016 Christian Gagneraud.
** Contact: http://www.qt.io/licensing
**
** This file is part of Qbs.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms and
** conditions see http://www.qt.io/terms-conditions. For further information
** use the contact form at http://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 or version 3 as published by the Free
** Software Foundation and appearing in the file LICENSE.LGPLv21 and
** LICENSE.LGPLv3 included in the packaging of this file.  Please review the
** following information to ensure the GNU Lesser General Public License
** requirements will be met: https://www.gnu.org/licenses/lgpl.html and
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, The Qt Company gives you certain additional
** rights.  These rights are described in The Qt Company LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
****************************************************************************/

#include "tst_clangdb.h"

#include "../shared.h"

#include <tools/hostosinfo.h>
#include <tools/installoptions.h>

#include <QDir>
#include <QFile>
#include <QRegExp>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <QtTest>

using qbs::InstallOptions;
using qbs::Internal::HostOsInfo;

int TestClangDb::runProcess(const QString &exec, const QStringList &args, QByteArray &stdErr,
                            QByteArray &stdOut)
{
    QProcess process;

    process.start(exec, args);
    const int waitTime = 10 * 60000;
    if (!process.waitForStarted() || !process.waitForFinished(waitTime)) {
        stdErr = process.readAllStandardError();
        return -1;
    }

    stdErr = process.readAllStandardError();
    stdOut = process.readAllStandardOutput();
    sanitizeOutput(&stdErr);
    sanitizeOutput(&stdOut);

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (!stdErr.isEmpty())
            qDebug("%s", stdErr.constData());
        if (!stdOut.isEmpty())
            qDebug("%s", stdOut.constData());
    }

    return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
}


TestClangDb::TestClangDb() : TestBlackboxBase(SRCDIR "/testdata-clangdb", "blackbox-clangdb"),
    projectDir(QDir::cleanPath(testDataDir + "/project1")),
    projectFileName("project.qbs"),
    buildDir(QDir::cleanPath(projectDir + "/" + relativeBuildDir())),
    sourceFilePath(QDir::cleanPath(projectDir + "/i like spaces.cpp")),
    dbFilePath(QDir::cleanPath(buildDir + "/compile_commands.json"))
{
}

TestClangDb::~TestClangDb()
{
}

void TestClangDb::initTestCase()
{
    TestBlackboxBase::initTestCase();
    QDir::setCurrent(projectDir);
}

void TestClangDb::ensureBuildTreeCreated()
{
    QCOMPARE(runQbs(), 0);
    QVERIFY(QFile::exists(buildDir));
}

void TestClangDb::checkCanGenerateDb()
{
    QbsRunParameters params;
    params.command = "generate";
    params.arguments << "--generator" << "clangdb";
    QCOMPARE(runQbs(params), 0);
    QVERIFY(QFile::exists(dbFilePath));
}

void TestClangDb::checkDbIsValidJson()
{
    QFile file(dbFilePath);
    QVERIFY(file.open(QFile::ReadOnly));
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QVERIFY(!doc.isNull());
    QVERIFY(doc.isArray());
}

void TestClangDb::checkDbIsConsistentWithProject()
{
    QFile file(dbFilePath);
    QVERIFY(file.open(QFile::ReadOnly));
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());

    // We expect only one command for now
    const QJsonArray array = doc.array();
    QVERIFY(array.count() == 1);

    // Validate the "command object"
    const QJsonObject entry = array.at(0).toObject();
    QVERIFY(entry.contains("directory"));
    QVERIFY(entry.value("directory").isString());
    QVERIFY(entry.contains("arguments"));
    QVERIFY(entry.value("arguments").isArray());
    QVERIFY(entry.value("arguments").toArray().count() >= 2);
    QVERIFY(entry.contains("file"));
    QVERIFY(entry.value("file").isString());
    QVERIFY(entry.value("file").toString() == sourceFilePath);

    // Validate the compile command itself, this requires a previous build since the command
    // line contains 'deep' path that are creating during Qbs build run
    QByteArray stdErr;
    QByteArray stdOut;
    QStringList arguments;
    const QJsonArray jsonArguments = entry.value("arguments").toArray();
    QString executable = jsonArguments.at(0).toString();
    for (int i=1; i<jsonArguments.count(); i++)
        arguments.append(jsonArguments.at(i).toString());
    QVERIFY(runProcess(executable, arguments, stdErr, stdOut) == 0);
}

// Run clang-check, should give 2 warnings:
// <...>/i like spaces.cpp:11:5: warning: Assigned value is garbage or undefined
//     int unused = garbage;
//     ^~~~~~~~~~   ~~~~~~~
// <...>/i like spaces.cpp:11:9: warning: Value stored to 'unused' during its initialization is never read
//     int unused = garbage;
//         ^~~~~~   ~~~~~~~
// 2 warnings generated.
void TestClangDb::checkClangDetectsSourceCodeProblems()
{
    QByteArray stdErr;
    QByteArray stdOut;
    QStringList arguments;
    const QString executable = findExecutable(QStringList("clang-check"));
    if (executable.isEmpty())
        QSKIP("No working clang-check executable found");
    arguments = QStringList() << "-analyze" << "-p" << relativeBuildDir() << sourceFilePath;
    QVERIFY(runProcess(executable, arguments, stdErr, stdOut) == 0);
    const QString output = QString::fromLocal8Bit(stdErr);
    QVERIFY(output.contains(QRegExp(QStringLiteral("warning.*undefined"), Qt::CaseInsensitive)));
    QVERIFY(output.contains(QRegExp(QStringLiteral("warning.*never read"), Qt::CaseInsensitive)));
}

QTEST_MAIN(TestClangDb)
