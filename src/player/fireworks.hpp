#pragma once
#include <QQuickItem>
#include <QObject>
#include <QQuickWindow>
#include <QOpenGLShaderProgram>
#include "renderer.hpp"
#include "platform.hpp"


class Fireworks : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool keepFrameRate READ isKeepFrameRate WRITE setKeepFrameRate NOTIFY keepFrameRateChanged)
    Q_PROPERTY(int frameHeight READ getHeight NOTIFY frameSizeChanged)
    Q_PROPERTY(int frameWidth READ getWidth NOTIFY frameSizeChanged)
    Q_PROPERTY(double frameRate READ getFrameRate NOTIFY frameSizeChanged)
    Q_PROPERTY(GLfloat brightness READ getBrightness WRITE setBrightness NOTIFY brightnessChanged)
    Q_PROPERTY(GLfloat contrast READ getContrast WRITE setContrast NOTIFY contrastChanged)
    Q_PROPERTY(GLfloat saturation READ getSaturation WRITE setSaturation NOTIFY saturationChanged)
    Q_PROPERTY(QString filterPrefix READ getFilterPrefix)
    Q_PROPERTY(QStringList filterJsons READ getFilterJsons)
private:
    FireworksRenderer *m_renderer;
    QString m_filterPrefix;
    QStringList m_filterJsons;
    int m_frameHeight = 1;
    int m_frameWidth = 1;
    double m_frameRate = 1.0;
protected:
    QSGNode *updatePaintNode(QSGNode *node, UpdatePaintNodeData *data) override {
        return m_renderer;
    }

public:
    explicit Fireworks(QQuickItem *parent = nullptr): QQuickItem(parent), m_renderer(new FireworksRenderer),
        m_filterPrefix(AnytMusic::getAssetsDir() + u"/filters"_qs), m_filterJsons() {
        QDir filterDir(m_filterPrefix);
        for(auto && filename : filterDir.entryList({"*.json"})) {
            QFile file = filterDir.filePath(filename);
            file.open(QIODevice::OpenModeFlag::ReadOnly);
            m_filterJsons.append(file.readAll());
            file.close();
        }
        this->setFlag(QQuickItem::ItemHasContents);
        connect(this, &QQuickItem::windowChanged, this, [this](QQuickWindow *win){
            qDebug() << "Window Size Changed:" << static_cast<void *>(win) << ".";
            if (win) {
                connect(this->window(), &QQuickWindow::beforeSynchronizing, m_renderer, &FireworksRenderer::sync, Qt::DirectConnection);
                connect(this->window(), &QQuickWindow::beforeRendering, m_renderer, &FireworksRenderer::init, Qt::DirectConnection);
                win->setColor(Qt::black);
            } else {
                qWarning() << "Window destroy.";
            }

        });
        qDebug() << "Create Hurricane QuickItem.";
    }
    ~Fireworks() override {
        m_renderer = nullptr;
    }

PONY_GUARD_BY(MAIN) private:
    [[nodiscard]] QString getFilterPrefix() const { return m_filterPrefix; }

    [[nodiscard]] bool isKeepFrameRate() const { return m_renderer->isKeepFrameRate(); }

    [[nodiscard]] QStringList getFilterJsons() const { return m_filterJsons; }

    [[nodiscard]] GLfloat getBrightness() const { return m_renderer->getBrightness(); }

    void setKeepFrameRate(bool keep) {
        m_renderer->setKeepFrameRate(keep);
        emit keepFrameRateChanged();
    }

    void setBrightness(GLfloat b) {
        m_renderer->setBrightness(b);
        emit brightnessChanged();
    }

    [[nodiscard]] GLfloat getContrast() const {
        return m_renderer->getContrast();
    }

    void setContrast(GLfloat c) {
        m_renderer->setContrast(c);
        emit contrastChanged();
    };

    [[nodiscard]] GLfloat getSaturation() const { return m_renderer->getSaturation(); };

    void setSaturation(GLfloat s) {
        m_renderer->setSaturation(s);
        emit saturationChanged();
    };

    [[nodiscard]] int getHeight() const {
        return m_frameHeight;
    }

    [[nodiscard]] int getWidth() const {
        return m_frameWidth;
    }

    [[nodiscard]] double getFrameRate() const {
        return m_frameRate;
    }


public slots:

    void setVideoFrame(const VideoFrameRef &pic) {
        // this function must be called on GUI thread
        // setImage -> sync -> render
        // since picture may use on renderer thread, we CANNOT free now
        // no change, return immediately
        if (m_renderer->setVideoFrame(pic)) {
            // make dirty
            this->update();
            if (!pic.isSameSize(m_frameWidth, m_frameHeight)) {
                m_frameWidth = pic.getWidth();
                m_frameHeight = pic.getHeight();
                m_frameRate = static_cast<double>(m_frameHeight) / static_cast<double>(m_frameWidth);
                emit frameSizeChanged();
            }
        }
    }

    /**
     * 设置LUT滤镜路径
     * @param path
     */
    Q_INVOKABLE void setLUTFilter(const QString& path) {
        QImage image;
        if (!path.isEmpty()) {
            image.load(QDir(m_filterPrefix).filePath(path));
            image.convertTo(QImage::Format_RGB888);
        }

        m_renderer->setLUTFilter(image);
    }



signals:
    void brightnessChanged();

    void contrastChanged();

    void saturationChanged();

    void frameSizeChanged();

    void keepFrameRateChanged();
};





