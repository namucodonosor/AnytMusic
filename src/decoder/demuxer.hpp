//
// Created by ColorsWind on 2022/5/6.
//
#pragma once

#include <QObject>
#include <utility>
#include "private/dispatcher.hpp"
#include "audioformat.hpp"

/**
 * 生命周期伴随整个程序运行.
 */
class Demuxer : public QObject {
    Q_OBJECT
    PONY_THREAD_AFFINITY(DECODER)
private:
    DemuxDispatcherBase *m_worker = nullptr;
    DecodeDispatcher *m_forward = nullptr;
    ReverseDecodeDispatcher *m_backward = nullptr;

    QThread *m_affinityThread = nullptr;
    std::mutex m_workerLock;
public:


    explicit Demuxer(QObject *parent) : QObject(nullptr) {
        m_affinityThread = new QThread;
        m_affinityThread->setObjectName(AnytMusic::DECODER);
        this->moveToThread(m_affinityThread);
        m_affinityThread->start();
    }

    ~Demuxer() override {
        qDebug() << "Destroy Demuxer";
        m_affinityThread->quit();
    }

    PONY_GUARD_BY(MAIN, FRAME, DECODER)

    VideoFrameRef getPicture() {
        std::unique_lock lock(m_workerLock);
        return m_worker->getPicture();
    }

    PONY_GUARD_BY(MAIN, FRAME, DECODER)

    qreal frontPicture() {
        std::unique_lock lock(m_workerLock);
        return m_worker->frontPicture();
    }

    PONY_THREAD_SAFE int skipPicture(const std::function<bool(qreal)> &predicate) {
        std::unique_lock lock(m_workerLock);
        return m_worker->skipPicture(predicate);
    }

    PONY_GUARD_BY(MAIN, FRAME, DECODER)

    AudioFrame getSample() {
        std::unique_lock lock(m_workerLock);
        return m_worker->getSample();
    }


    PONY_GUARD_BY(MAIN, FRAME, DECODER)

    qreal frontSample() {
        std::unique_lock lock(m_workerLock);
        return m_worker->frontSample();
    }

    PONY_THREAD_SAFE int skipSample(const std::function<bool(qreal)> &predicate) {
        std::unique_lock lock(m_workerLock);
        return m_worker->skipSample(predicate);
    }


    PONY_GUARD_BY(MAIN, FRAME, DECODER)

    qreal audioDuration() {
        std::unique_lock lock(m_workerLock);
        return m_forward ? m_forward->getAudionLength() : 0.0;
    }

    PONY_GUARD_BY(MAIN, FRAME, DECODER)

    qreal videoDuration() {
        std::unique_lock lock(m_workerLock);
        return m_forward ? m_forward->getVideoLength() : 0.0;
    }

    PONY_GUARD_BY(MAIN, FRAME, DECODER)

    QStringList getTracks() {
        std::unique_lock lock(m_workerLock);
        if (m_forward) {
            return m_forward->getTracks();
        } else {
            return {u"没有打开的文件"_qs};
        }
    }

    /**
     * 当前是否倒放
     * @return
     */
    PONY_THREAD_SAFE bool isBackward() {
        std::unique_lock lock(m_workerLock);
        return dynamic_cast<ReverseDecodeDispatcher *>(m_worker);
    }


    PONY_THREAD_SAFE bool hasVideo() {
        std::unique_lock lock(m_workerLock);
        return m_forward && m_forward->hasVideo();
    }



    /**
     * 向 DecodeThread 发送信号尽快暂停解码, 并唤醒阻塞在上面的线程.
     * @see DecodeDispatcher::statePause
    */
    PONY_CONDITION("OpenFileResult")
    PONY_THREAD_SAFE void pause() {
        std::unique_lock lock(m_workerLock);
        m_worker->statePause();
    }

    PONY_THREAD_SAFE bool isFileOpen() {
        std::unique_lock lock(m_workerLock);
        return m_worker != nullptr;
    }

    /**
     * 清空旧的帧, 这个方法会阻塞直到队列中的所有旧帧清理完成.
     * @see DecodeDispatcher::flush
     */
    PONY_GUARD_BY(FRAME) void flush() {
        std::unique_lock lock(m_workerLock);
        m_worker->flush();
    }

    /**
     * 在 DecodeThread 启动解码器, 这个方法是非阻塞的, 但是可以保证返回后队里请求能够被阻塞.
     */
    PONY_THREAD_SAFE void start() {
        std::unique_lock lock(m_workerLock);
        qDebug() << "Start Decoder";
        m_worker->stateResume();
    }

    void setEnableAudio(bool enable) {
        m_worker->setEnableAudio(enable);
    }

    PonyAudioFormat getInputFormat() {
        std::unique_lock lock(m_workerLock);
        if (m_worker) {
            return m_worker->getAudioInputFormat();
        } else {
            return AnytMusic::DEFAULT_AUDIO_FORMAT;
        }

    }

    /**
     * 设置 demuxer 输出格式, 必须保证 demuxer 已停止, 需要重新 seek 才能保证获取到正确的帧
     * @param format
     */
    void setOutputFormat(PonyAudioFormat format) {
        std::unique_lock lock(m_workerLock);
        m_forward->setAudioOutputFormat(format);
        m_backward->setAudioOutputFormat(std::move(format));
    }


public slots:

    /**
     * 调整视频进度, 必须保证解码器线程空闲且缓冲区为空. 方法返回后保证产生的帧是在时间正确. \n
     * 一次完整的调整进度操作应该为: \n
     * 1. 在VideoThread线程调用 Demuxer2::statePause 使解码器线程停止运行; \n
     * 2. 在VideoThread线程调用 Demuxer2::seek 并阻塞等待函数返回, 接下来产生的帧是新的帧. \n
     * 3. 在VideoThread线程调用 Demuxer2::flush 清空队列中的旧帧. \n
     * 4. 在DeocdeThread线程中执行 Demuxer2::start, 恢复解码器线程线程运行. \n
     * @param secs 视频进度(单位: s)
     * @see Demuxer2::statePause
     * @see Demuxer2::flush
     * @see DecodeDispatcher::seek
     */
    void seek(qreal secs) {
        m_worker->seek(secs);
    }

    /**
     * 设置音频索引, 必须保证解码器线程空闲且缓冲区为空
     * @param index
     * @see DecodeDispatcher::seek
     */
    void setAudioIndex(StreamIndex index) {
        m_forward->setAudioIndex(index);
    }

    /**
     * 打开文件
     * @param fn 本地文件路径
     */
    void openFile(const std::string &fn) {
        qDebug() << "Demuxer Open file" << QString::fromUtf8(fn);
        // call on video decoder thread
        std::unique_lock lock(m_workerLock);
        if (m_worker) {
            qWarning() << "Already open file:" << m_worker->filename.c_str();
            emit openFileResult(AnytMusic::OpenFileResultType::FAILED, QPrivateSignal());
            return;
        }
        AnytMusic::OpenFileResultType result;
        try {
            m_forward = new DecodeDispatcher(fn, result, DEFAULT_STREAM_INDEX, DEFAULT_STREAM_INDEX, this);
            m_backward = new ReverseDecodeDispatcher(fn, this);
            m_worker = m_forward;
        } catch (std::runtime_error &ex) {
            qWarning() << "Error opening file:" << ex.what();
            m_worker = nullptr;
            m_backward = nullptr;
            m_forward = nullptr;
            emit openFileResult(result, QPrivateSignal());
            return;
        }
        lock.unlock();
        m_worker->stateResume();
        emit openFileResult(result, QPrivateSignal());
        qDebug() << "Open file success.";
    }


    /**
     * 倒放视频, 必须保证解码器线程空闲且缓冲区为空. 方法返回后保证产生的帧是在时间正确.
     * @param secs 视频进度(单位: s)
     * @see Demuxer2::statePause
     * @see Demuxer2::flush
     * @see DecodeDispatcher::seek
     */
    void backward() {
        std::unique_lock lock(m_workerLock);
        m_worker = m_backward;
        m_forward->flush();
    }

    /**
     * 正向播放视频, 必须保证解码器线程空闲且缓冲区为空. 方法返回后保证产生的帧是在时间正确.
     * @param secs 视频进度(单位: s)
     * @see Demuxer2::statePause
     * @see Demuxer2::flush
     * @see DecodeDispatcher::seek
     */
    void forward() {
        std::unique_lock lock(m_workerLock);
        m_worker = m_forward;
        m_backward->flush();
    };

    void close() {
        std::unique_lock lock(m_workerLock);
        if (m_worker) {
            qDebug() << "Close file" << m_worker->filename.c_str();
            m_worker = nullptr;
            if (m_forward) {
                m_forward->statePause();
                m_forward->deleteLater();
                m_forward = nullptr;
            }
            if (m_backward) {
                m_backward->statePause();
                m_backward->deleteLater();
                m_backward = nullptr;
            }
        } else {
            qWarning() << "Try to close file while no file has been opened.";
        }
    }

    void setTrack(int i) {
        std::unique_lock lock(m_workerLock);
        m_worker->setTrack(i);
    }


    void test_onWork() {
        m_worker->test_onWork();
    }

signals:

    void openFileResult(AnytMusic::OpenFileResultType result, QPrivateSignal);
};

