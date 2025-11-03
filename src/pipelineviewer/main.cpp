#include <QApplication>
#include "MainWindow.h"
#include <string>
#include <iostream>

int main(int argc, char *argv[]) {
    // Defaults: image directory and pipeline XML
    std::string image_dir = "build/bin/data-frames";
    std::string pipeline_xml = "build/bin/pipeline_test3_config.xml";
    // Positional overrides if provided
    if (argc >= 2) image_dir = argv[1];
    if (argc >= 3) pipeline_xml = argv[2];
    if (argc == 2 && (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help")) {
        std::cerr << "Usage: " << argv[0] << " [image_directory] [pipeline_xml]" << std::endl;
        std::cerr << "Defaults: image_directory='" << image_dir << "', pipeline_xml='" << pipeline_xml << "'\n";
        return 0;
    }

    QApplication app(argc, argv);
    MainWindow w(QString::fromStdString(image_dir), QString::fromStdString(pipeline_xml));
    w.show();
    return app.exec();
}
