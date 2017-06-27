/****************************************************************************
**
** Copyright (C) 2014 Klaralvdalens Datakonsult AB, a KDAB Group company, info@kdab.com, author Sergio Martins <sergio.martins@kdab.com>
** Contact: http://www.qt-project.org/legal
**
** This file is part of the plugins of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL21$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia. For licensing terms and
** conditions see http://qt.digia.com/licensing. For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 or version 3 as published by the Free
** Software Foundation and appearing in the file LICENSE.LGPLv21 and
** LICENSE.LGPLv3 included in the packaging of this file. Please review the
** following information to ensure the GNU Lesser General Public License
** requirements will be met: https://www.gnu.org/licenses/lgpl.html and
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights. These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QCommandLineParser>
#include <QGuiApplication>

#include <QtQml/private/qqmljslexer_p.h>
#include <QtQml/private/qqmljsparser_p.h>
#include <QtQml/private/qqmljsengine_p.h>

# include <QtQml/private/qqmlirbuilder_p.h>

static bool s_silent = false;

static void remove_metadata(QString &code)
{
    QmlIR::Document::removeScriptPragmas(code);
}

static QStringList semanticBlackList()
{
    static QStringList blacklist;
    if (blacklist.isEmpty()) {
        blacklist << QLatin1String("is not installed")
                  << QLatin1String(" unavailable")
                  << QLatin1String(": no such directory")
                  << QLatin1String(": File not found");
    }

    return blacklist;
}


void myMessageOutput(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    if (s_silent)
        return;

    QByteArray localMsg = msg.toLocal8Bit();
    switch (type) {
    case QtDebugMsg:
    case QtWarningMsg:
    case QtCriticalMsg:
        fprintf(stderr, "%s\n", localMsg.constData());
        break;
    case QtFatalMsg:
        fprintf(stderr, "Fatal: %s (%s:%u, %s)\n", localMsg.constData(), context.file, context.line, context.function);
        abort();
    }
}

static bool lint_file(const QString &filename)
{
    QFile file(filename);
    if (!file.open(QFile::ReadOnly)) {
        qWarning() << "Failed to open file" << filename << file.error();
        return false;
    }

    QString code = file.readAll();
    file.close();

    QQmlJS::Engine engine;
    QQmlJS::Lexer lexer(&engine);

    QFileInfo info(filename);
    bool isJavaScript = info.suffix().toLower() == QLatin1String("js");
    if (isJavaScript) {
        remove_metadata(/*by-ref*/code);
    }

    lexer.setCode(code, /*line = */ 1, true);
    QQmlJS::Parser parser(&engine);

    bool success = isJavaScript ? parser.parseProgram() : parser.parse();

    if (!success) {
        foreach (const QQmlJS::DiagnosticMessage &m, parser.diagnosticMessages()) {
            qWarning("%s:%d : %s", qPrintable(filename), m.loc.startLine, qPrintable(m.message));
        }
    }

    return success;
}

static QQmlEngine* qmlEngine()
{
    static QQmlEngine *engine = new QQmlEngine();
    return engine;
}

static bool run_semantic_checks(const QString &filename)
{
    bool success = true;
    if (!filename.endsWith(QLatin1String(".qml")))
        return true;

    QQmlComponent component(qmlEngine(), QUrl::fromLocalFile(filename));
    bool tmp = s_silent;
    s_silent = true;
    QObject *obj = component.create();
    s_silent = tmp;
    if (!obj) {
        foreach (const QQmlError &error, component.errors()) {
            const QString errorStr = error.toString();
            bool blacklisted = false;
            foreach (const QString &exp, semanticBlackList()) {
                if (errorStr.contains(exp)) {
                    blacklisted = true;
                    break;
                }
            }

            success &= blacklisted;
            if (!blacklisted) {
                qWarning() << error;
            }
        }
    }

    return success;
}

int main(int argv, char *argc[])
{
    QGuiApplication app(argv, argc);
    QGuiApplication::setApplicationName("qmllint");
    QGuiApplication::setApplicationVersion("1.0");
    QCommandLineParser parser;
    parser.setApplicationDescription(QLatin1String("QML syntax verifier"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption silentOption(QStringList() << "s" << "silent", QLatin1String("Don't output syntax errors"));
    QCommandLineOption semanticOption(QStringList() << "t" << "semantic", QLatin1String("Run semantic checks too"));
    parser.addOption(silentOption);
    parser.addOption(semanticOption);
    parser.addPositionalArgument(QLatin1String("files"), QLatin1String("list of qml or js files to verify"));

    parser.process(app);
    qInstallMessageHandler(myMessageOutput);

    if (parser.positionalArguments().isEmpty()) {
        parser.showHelp(-1);
    }

    s_silent = parser.isSet(silentOption);
    bool semantic = parser.isSet(semanticOption);
    bool success = true;
    foreach (const QString &filename, parser.positionalArguments()) {
        success &= lint_file(filename);
    }

    if (success && semantic) {
        foreach (const QString &filename, parser.positionalArguments()) {
            success &= run_semantic_checks(filename);
        }
    }

    return success ? 0 : -1;
}
