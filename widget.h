#ifndef WIDGET_H
#define WIDGET_H

#include <memory>

#include <QWidget>
#include <QImage>
#include <QLabel>

class DatabaseInterface;

class Widget : public QWidget
{
    Q_OBJECT

public:
    Widget(QWidget *parent = nullptr);
    ~Widget();

public slots:
    void recognize();
    void registerFace();

private:
    auto getOpenImageFileName(
        QString const & title,
        QString const & path = ""
    ) -> QString;
    auto displayScaledImage() -> void;
    auto lookup(std::vector<uint8_t> const & target, float threshold) -> QString;
    auto drawFaceFrame(QRect const & rect, QString const & name) -> void;

private:
    QImage origin_;
    QImage replica_;
    QLabel preview_;

    std::unique_ptr<DatabaseInterface> database_;
    void * face_engine_ = nullptr;

private:
    static QSize screen_size;
    static int title_bar_height;
};

#endif // WIDGET_H
