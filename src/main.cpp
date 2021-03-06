/*
 * nheko Copyright (C) 2017  Konstantinos Sideris <siderisk@auth.gr>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <iostream>

#include <QApplication>
#include <QCommandLineParser>
#include <QDesktopWidget>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QLabel>
#include <QLibraryInfo>
#include <QMessageBox>
#include <QPoint>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QTranslator>

#include "Config.h"
#include "Logging.h"
#include "MainWindow.h"
#include "MatrixClient.h"
#include "Utils.h"
#include "config/nheko.h"
#include "singleapplication.h"

#if defined(Q_OS_MAC)
#include "emoji/MacHelper.h"
#endif

#ifdef QML_DEBUGGING
#include <QQmlDebuggingEnabler>
QQmlDebuggingEnabler enabler;
#endif

#if defined(Q_OS_LINUX)
#include <boost/stacktrace.hpp>
#include <csignal>

void
stacktraceHandler(int signum)
{
        std::signal(signum, SIG_DFL);
        boost::stacktrace::safe_dump_to("./nheko-backtrace.dump");
        std::raise(SIGABRT);
}

void
registerSignalHandlers()
{
        std::signal(SIGSEGV, &stacktraceHandler);
        std::signal(SIGABRT, &stacktraceHandler);
}

#else

// No implementation for systems with no stacktrace support.
void
registerSignalHandlers()
{}

#endif

QPoint
screenCenter(int width, int height)
{
        // Deprecated in 5.13: QRect screenGeometry = QApplication::desktop()->screenGeometry();
        QRect screenGeometry = QGuiApplication::primaryScreen()->geometry();

        int x = (screenGeometry.width() - width) / 2;
        int y = (screenGeometry.height() - height) / 2;

        return QPoint(x, y);
}

void
createCacheDirectory()
{
        auto dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);

        if (!QDir().mkpath(dir)) {
                throw std::runtime_error(
                  ("Unable to create state directory:" + dir).toStdString().c_str());
        }
}

int
main(int argc, char *argv[])
{
#if defined(Q_OS_LINUX) || defined(Q_OS_WIN) || defined(Q_OS_FREEBSD)
        if (qgetenv("QT_SCALE_FACTOR").size() == 0) {
                float factor = utils::scaleFactor();

                if (factor != -1)
                        qputenv("QT_SCALE_FACTOR", QString::number(factor).toUtf8());
        }
#endif

        QCoreApplication::setApplicationName("nheko");
        QCoreApplication::setApplicationVersion(nheko::version);
        QCoreApplication::setOrganizationName("nheko");
        QCoreApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);
        QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
        QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
        SingleApplication app(argc,
                              argv,
                              false,
                              SingleApplication::Mode::User |
                                SingleApplication::Mode::ExcludeAppPath |
                                SingleApplication::Mode::ExcludeAppVersion);

        QCommandLineParser parser;
        parser.addHelpOption();
        parser.addVersionOption();
        QCommandLineOption debugOption("debug", "Enable debug output");
        parser.addOption(debugOption);
        parser.process(app);

        app.setWindowIcon(QIcon(":/logos/nheko.png"));

        http::init();

        createCacheDirectory();

        registerSignalHandlers();

        if (parser.isSet(debugOption))
                nhlog::enable_debug_log_from_commandline = true;

        try {
                nhlog::init(QString("%1/nheko.log")
                              .arg(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
                              .toStdString());
        } catch (const spdlog::spdlog_ex &ex) {
                std::cout << "Log initialization failed: " << ex.what() << std::endl;
                std::exit(1);
        }

        QSettings settings;

        QFont font;
        QString userFontFamily = settings.value("user/font_family", "").toString();
        if (!userFontFamily.isEmpty()) {
                font.setFamily(userFontFamily);
        }
        font.setPointSizeF(settings.value("user/font_size", font.pointSizeF()).toDouble());

        app.setFont(font);

        QString lang = QLocale::system().name();

        QTranslator qtTranslator;
        qtTranslator.load("qt_" + lang, QLibraryInfo::location(QLibraryInfo::TranslationsPath));
        app.installTranslator(&qtTranslator);

        QTranslator appTranslator;
        appTranslator.load("nheko_" + lang, ":/translations");
        app.installTranslator(&appTranslator);

        MainWindow w;

        // Move the MainWindow to the center
        w.move(screenCenter(w.width(), w.height()));

        if (!settings.value("user/window/start_in_tray", false).toBool() ||
            !settings.value("user/window/tray", true).toBool())
                w.show();

        QObject::connect(&app, &QApplication::aboutToQuit, &w, [&w]() {
                w.saveCurrentWindowSize();
                if (http::client() != nullptr) {
                        nhlog::net()->debug("shutting down all I/O threads & open connections");
                        http::client()->close(true);
                        nhlog::net()->debug("bye");
                }
        });
        QObject::connect(&app, &SingleApplication::instanceStarted, &w, [&w]() {
                w.show();
                w.raise();
                w.activateWindow();
        });

#if defined(Q_OS_MAC)
        // Temporary solution for the emoji picker until
        // nheko has a proper menu bar with more functionality.
        MacHelper::initializeMenus();
#endif

        nhlog::ui()->info("starting nheko {}", nheko::version);

        return app.exec();
}
