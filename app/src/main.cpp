#include <QApplication>
#include "MainWindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("rp2040joy");
    app.setOrganizationName("rp2040joy");

    MainWindow w;
    w.show();
    return app.exec();
}
