#include <QApplication>
#include <QIcon>

#include "mainwidget.h"

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    const QIcon appIcon(QStringLiteral(":/images/home.png"));
    application.setWindowIcon(appIcon);

    MainWidget mainWidget;
    mainWidget.setWindowIcon(appIcon);
    mainWidget.show();

    return application.exec();
}
