#include "widget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>

#include <QPushButton>

#include <QImage>
#include <QPixmap>

#include <QPainter>

#include <QFileDialog>
#include <QDir>
#include <QFileInfo>

#include <QDebug>

#include <QGuiApplication>
#include <QScreen>

#include <QStyle>
#include <QSettings>

#pragma warning(push, 0)

#include <arcsoft_face_sdk.h>
#include <merror.h>

#pragma warning(pop)

#include "database_sqlite.h"
#include "database_mysql.h"

namespace
{

auto loadImage(QString const & filename) -> QImage
{
    auto image = QImage();

    if (!image.load(filename))
    {
        qDebug() << "Failed to load image file: " << filename;
        return image;
    }

    // 4 倍向下对齐后的宽度
    auto const expect_width = image.width() & (-4);

    if (image.width() == expect_width) { return image; }

    image = image.copy(0, 0, expect_width, image.height());

    return image;
}

auto toB8G8R8(QImage const & image) -> QImage
{
    auto image2 = image.convertToFormat(QImage::Format_RGB888);
    auto const width = image2.width();
    auto const height = image2.height();
    auto const data = image2.bits();
    for (auto i = 0; i != 3 * width * height; i += 3)
    {
        std::swap(data[i+0], data[i+2]);
    }
    return image2;
}

auto activateArcFaceSDK() -> void
{
    auto && settings = QSettings("profile.ini", QSettings::IniFormat);
    auto app_id = settings.value("app-id").toByteArray();
    auto sdk_key = settings.value("sdk-key").toByteArray();

    auto const ret = ASFOnlineActivation(app_id.data(), sdk_key.data());
    if (ret != MOK && ret != MERR_ASF_ALREADY_ACTIVATED)
    {
        throw std::runtime_error("Failed to activate arcface sdk: " + std::to_string(ret));
    }
}

auto initArcFaceSDK() -> void *
{
    auto handle = MHandle();
    auto const ret = ASFInitEngine(
        ASF_DETECT_MODE_IMAGE,
        ASF_OP_0_ONLY,
        32,
        10,
        ASF_FACE_DETECT | ASF_FACERECOGNITION,
        &handle
    );
    if (ret != MOK)
    {
        throw std::runtime_error("Failed to init arcface sdk: " + std::to_string(ret));
    }
    return handle;
}

auto toASVLOFFSCREEN(QImage const & image) -> ASVLOFFSCREEN
{
    auto img = ASVLOFFSCREEN();

    img.u32PixelArrayFormat = ASVL_PAF_RGB24_B8G8R8;
    img.i32Width = image.width();
    img.i32Height = image.height();
    img.pi32Pitch[0] = 3 * image.width();
    img.ppu8Plane[0] = const_cast<uint8_t *>(image.bits());

    return img;
}

auto detectFaces(
    MHandle const handle,
    QImage const & image
) -> ASF_MultiFaceInfo
{
    auto img = toASVLOFFSCREEN(image);

    auto faces_info = ASF_MultiFaceInfo();    

    auto const ret = ASFDetectFacesEx(
        handle,
        &img,
        &faces_info
    );

    if (ret != MOK)
    {
        throw std::runtime_error("Failed to detect faces: " + std::to_string(ret));
    }

    qDebug() << faces_info.faceNum << "faces detected";

    return faces_info;
}

auto toASFFeature(std::vector<uint8_t> const & feature) -> ASF_FaceFeature
{
    auto asf_feat = ASF_FaceFeature();
    // 虽然 .feature 是非 const 的，但内部其实不会修改
    asf_feat.feature = const_cast<uint8_t *>(feature.data());
    asf_feat.featureSize = static_cast<int>(feature.size());

    return asf_feat;
}

auto fromASFFeature(ASF_FaceFeature const & asf_feat) -> std::vector<uint8_t>
{
    assert(asf_feat.featureSize != 0);
    auto feature = std::vector<uint8_t>(static_cast<size_t>(asf_feat.featureSize));

    std::copy_n(asf_feat.feature, asf_feat.featureSize, feature.begin());

    return feature;
}

auto extractFeature(
    MHandle const handle,
    QImage const & image,
    ASF_SingleFaceInfo & face_info
) -> std::vector<uint8_t>
{
    auto img = toASVLOFFSCREEN(image);
    auto feature = ASF_FaceFeature();
    auto const ret = ASFFaceFeatureExtractEx(
        handle,
        &img,
        &face_info,
        &feature
    );
    if (ret == MOK) { return fromASFFeature(feature); }

    if (ret == MERR_FSDK_FACEFEATURE_LOW_CONFIDENCE_LEVEL)
    {
        qDebug() << "Face feature low confidence level";
        return {};
    }

    throw std::runtime_error("Failed to extract face feature: " + std::to_string(ret));
}

}   // namespace


QSize Widget::screen_size;
int   Widget::title_bar_height;

#include <QRgb>
#include <QColor>

Widget::Widget(QWidget *parent)
    : QWidget(parent)
{
    if (Widget::screen_size.isEmpty())
    {
        Widget::screen_size = QSize(1280, 720);
        auto const & screens = QGuiApplication::screens();
        if (!screens.empty())
        {
            Widget::screen_size = screens.front()->availableSize();
        }
        qDebug() << "Screen Size:" << Widget::screen_size;

        Widget::title_bar_height = this->style()->pixelMetric(QStyle::PM_TitleBarHeight);
    }

    ::activateArcFaceSDK();
    face_engine_ = ::initArcFaceSDK();

    database_.reset(new DatabaseSQLite("faces_feature.db"));
//    database_.reset(new DatabaseMySQL("localhost", "root", "", "faces_feature"));

    auto const layout = new QVBoxLayout();

    {
        layout->addWidget(&preview_);

        auto image = QImage(400, 300, QImage::Format_RGB888);
        image.fill(QColor(255, 255, 242));
        preview_.setPixmap(QPixmap::fromImage(image));
    }

    {
        auto const h_layout = new QHBoxLayout();

        auto const recognize_button = new QPushButton("识别");
        h_layout->addWidget(recognize_button);

        auto const register_face_button = new QPushButton("注册");
        h_layout->addWidget(register_face_button);

        layout->addLayout(h_layout);

        connect(recognize_button, &QPushButton::clicked, this, &Widget::recognize);
        connect(register_face_button, &QPushButton::clicked, this, &Widget::registerFace);
    }

    this->setLayout(layout);
}

Widget::~Widget()
{
    ASFUninitEngine(face_engine_);
}

auto Widget::recognize() -> void
{
    qDebug() << "recognize";
    auto const filename = this->getOpenImageFileName(tr("选择需要识别的图片"));
    if (filename.isEmpty()) { return; }

    if ((origin_ = ::loadImage(filename)).isNull()) { return; }

    this->displayScaledImage();

    auto image_bgr = ::toB8G8R8(origin_);
    auto const & faces_info = ::detectFaces(face_engine_, image_bgr);

    if (faces_info.faceNum == 0) { return; }

    auto const ratio = 1.f * replica_.width() / origin_.width();

    for (auto i = 0; i != faces_info.faceNum; ++i)
    {
        auto face_info = ASF_SingleFaceInfo();
        face_info.faceRect = faces_info.faceRect[i];
        face_info.faceOrient = faces_info.faceOrient[i];

        auto const & feature = ::extractFeature(face_engine_, image_bgr, face_info);

        auto const & rect = faces_info.faceRect[i];

        this->drawFaceFrame(
            QRect(
                std::lround(rect.left * ratio),
                std::lround(rect.top * ratio),
                std::lround((rect.right - rect.left + 1) * ratio),
                std::lround((rect.bottom - rect.top + 1) * ratio)
            ),
            this->lookup(feature, 0.8f));
    }
}

auto Widget::registerFace() -> void
{
    qDebug() << "register";
    auto const & filename = this->getOpenImageFileName(tr("选择需要注册包含人脸的图片"));
    if (filename.isEmpty()) { return; }

    if ((origin_ = ::loadImage(filename)).isNull()) { return; }

    auto image_bgr = ::toB8G8R8(origin_);
    auto const & faces_info = ::detectFaces(face_engine_, image_bgr);

    if (faces_info.faceNum == 0){ return; }

    auto face_info = ASF_SingleFaceInfo();
    face_info.faceRect = faces_info.faceRect[0];
    face_info.faceOrient = faces_info.faceOrient[0];

    auto feature = ::extractFeature(face_engine_, image_bgr, face_info);
    if (feature.empty()) { return; }

    auto name = QFileInfo(filename).baseName();
    auto const success = database_->add(name, std::move(feature));
    qDebug() << (success? "Successfully added": "Failed to add") << name;
}

auto Widget::getOpenImageFileName(
    QString const & title,
    QString const & path
) -> QString
{
    auto filename = QFileDialog::getOpenFileName(
        this,
        title,
        !path.isEmpty()? path: QDir::currentPath(),
        tr("Image Files (*.png *.jpg *.bmp)")
    );

    qDebug() << filename;

    return filename;
}

auto Widget::displayScaledImage() -> void
{
    auto const origin_size = origin_.size();

    auto const reserved_height = 2.f * (this->height()-preview_.height());
    auto const ratio = std::min(
        Widget::screen_size.width() / 2.f / origin_size.width(),
        (Widget::screen_size.height() - reserved_height) / origin_size.height()
    );

    auto const replica_size = QSize(
        std::lround(origin_size.width() * ratio),
        std::lround(origin_size.height() * ratio)
    );

    qDebug() << origin_size << "-->" << replica_size;
    replica_ = origin_.scaled(replica_size, Qt::KeepAspectRatioByExpanding);
    preview_.setPixmap(QPixmap::fromImage(replica_));
    this->adjustSize();
    auto const & geo = this->geometry();
    if (screen_size.width() <= geo.right() || screen_size.height() <= geo.bottom())
    {
        auto const dx = std::min(screen_size.width()-1 - geo.right(), 0);
        auto const dy = std::min(screen_size.height()-1 - geo.bottom(), 0);
        auto const target = QPoint(this->x() + dx, this->y() + dy);
        this->move(target);
        qDebug() << "Move to:" << target;
    }
}

auto Widget::lookup(
    std::vector<uint8_t> const & target,
    float const threshold
) -> QString
{
    if (target.empty()) { return QStringLiteral(u"?"); }

    auto asf_feat = ::toASFFeature(target);

    auto name = QString("?");
    auto max_similarity = 0.0f;

    for (auto const & feat_name: database_->features())
    {
        auto asf_feat2 = ::toASFFeature(feat_name.first);
        auto similarity = 0.0f;
        ASFFaceFeatureCompare(
            face_engine_,
            &asf_feat,
            &asf_feat2,
            &similarity
        );
        if (threshold <= similarity && max_similarity < similarity)
        {
            name = feat_name.second;
            max_similarity = similarity;
            qDebug() << "Found" << name << ':' << max_similarity;
        }
    }

    return name;
}

auto Widget::drawFaceFrame(QRect const & rect, QString const & name) -> void
{
    auto && painter = QPainter(&replica_);

    auto pen = QPen(
        name != QStringLiteral(u"?")? QColor(0, 255, 0): QColor(255, 0, 0)
    );
    pen.setWidth(2);
    painter.setPen(pen);

    auto font = QFont();
    font.setPixelSize(20);
    painter.setFont(font);

    constexpr auto border_width = 2;
    auto const target = QRect(
        border_width,
        border_width,
        replica_.width() - 2 * border_width,
        replica_.height() - 2 * border_width
    ).intersected(rect);

    painter.drawRect(target);
    painter.drawText(
        target.x() + 2,
        target.y() + target.height() - 4,
        name
    );

    // 每一次都刷新界面会更慢，但视觉效果更好
    preview_.setPixmap(QPixmap::fromImage(replica_));
    QCoreApplication::processEvents();
}
