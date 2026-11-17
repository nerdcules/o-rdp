// o-rdp - find the PCs with Remote Desktop on the local network and connect
// to them; the window version of rdp-connect.

#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>

#include "backend.h"
#include "cli.h"
#include "theme.h"
#include "translator.h"

int main(int argc, char *argv[]) {
    QStringList arguments;
    for (int i = 0; i < argc; ++i)
        arguments.append(QString::fromLocal8Bit(argv[i]));

    // The display-free commands skip the GUI stack entirely.
    if (isCliCommand(arguments)) {
        QCoreApplication app(argc, argv);
        app.setApplicationName("o-rdp");
        return runCli(arguments);
    }

    QGuiApplication app(argc, argv);
    app.setApplicationName("o-rdp");
    app.setApplicationVersion("0.1.0");
    // Matches the window to o-rdp.desktop, so the compositor and the
    // launcher pick up the installed icon.
    app.setDesktopFileName("o-rdp");
    app.setWindowIcon(QIcon::fromTheme("o-rdp"));
    QQuickStyle::setStyle("Material");

    Translator translator;
    app.installTranslator(&translator);
    Theme theme;
    Backend backend;

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("rdp", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    backend.scan();
    return app.exec();
}
