#include <QApplication>
#include <QString>
#include "MainWindow.h"
#include "TaggingWindow.h"
#include "LandingDialog.h"
#include <string>
#include <iostream>
#include <exception>
#include <QMessageBox>

int main(int argc, char *argv[]) {
    try {
        if (argc >= 2 && (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help")) {
            std::cerr << "Usage: " << argv[0] << " [--tags-db <path>]" << std::endl;
            std::cerr << "Pipeline Viewer - Connect to record3k API and view recordings\n";
            std::cerr << "Use the GUI to configure API connection and load records\n";
            std::cerr << "  --tags-db <path>  sqlite store for Tag Record mode "
                          "(default: " << TaggingWindow::kDefaultTagsDbPath << ")\n";
            return 0;
        }

        QString tagsDbPath;
        for (int i = 1; i < argc; ++i) {
            if (std::string(argv[i]) == "--tags-db" && i + 1 < argc) {
                tagsDbPath = QString::fromLocal8Bit(argv[i + 1]);
                ++i;
            }
        }

        QApplication app(argc, argv);

        LandingDialog landing;
        if (landing.exec() != QDialog::Accepted) {
            return 0;
        }

        if (landing.selectedMode() == LandingDialog::Mode::TagRecord) {
            TaggingWindow w(tagsDbPath);
            w.show();
            return app.exec();
        } else {
            MainWindow w;
            w.show();
            return app.exec();
        }
    } catch (const std::exception& e) {
        QMessageBox::critical(nullptr, "Fatal Error", 
            QString("Application crashed with error:\n\n") + e.what());
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        QMessageBox::critical(nullptr, "Fatal Error", 
            "Application crashed with unknown error");
        std::cerr << "Fatal error: unknown exception" << std::endl;
        return 1;
    }
}
