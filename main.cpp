#include "widget.h"
#include <QApplication>

#include <QDebug>

int main(int argc, char *argv[])
{
    try
    {
        QApplication a(argc, argv);
        Widget w;
        w.show();
        return a.exec();
    }
    catch (std::exception const & e)
    {
        qDebug() << e.what();
    }

}
