#include <QApplication>
#include "MainWindow.h"
#include <string>
#include <iostream>
#include <exception>
#include <QMessageBox>

int main(int argc, char *argv[]) {
    try {
        if (argc >= 2 && (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help")) {
            std::cerr << "Usage: " << argv[0] << std::endl;
            std::cerr << "Pipeline Viewer - Connect to record3k API and view recordings\n";
            std::cerr << "Use the GUI to configure API connection and load records\n";
            return 0;
        }

        QApplication app(argc, argv);
        MainWindow w;
        w.show();
        return app.exec();
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
