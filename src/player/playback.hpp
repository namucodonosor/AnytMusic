//
// Created by ColorsWind on 2022/5/5.
//
#pragma once
#ifndef PONYPLAYER_VIDEOWORKER_H
#define PONYPLAYER_VIDEOWORKER_H
#include <QObject>
#include <QThread>
#include <QDebug>
#include <QCoreApplication>
#include "demuxer.hpp"
#include "audiosink.h"
#include "frame.hpp"

/**
 * @brief 负责输出视频和音频(不含视频预览).
 *
 * 这个类负责将上层的帧输出到相应的设备. 这个类的RAII的. 如果没有特殊说明, 这个类的公有方法是线程安全的.
 */
class Playback : public QObject {
    Q_OBJECT
private:
    QThread *m_affinityThread;
    Demuxer *m_demuxer;


    PonyAudioSink *m_audioSink;
    std::atomic<bool> m_isInterrupt;
    std::atomic<bool> m_isPlaying;
    std::mutex m_interruptMutex;
    std::mutex m_workMutex;
    std::condition_variable m_interruptCond;

    inline void changeState(bool isPlaying) {
        m_isPlaying = isPlaying;
        emit stateChanged(isPlaying);
    }

    inline void syncTo(const VideoFrame &frame) {
        if (!frame.isValid()) { return; }
        double duration;
        if (frame.isPlaceholderFrame()) {
            duration = 1. / 30;
        } else {
            bool backward = m_demuxer->isRewind();
            duration = frame.getPTS() - m_audioSink->getProcessSecs(backward);
            if (backward) { duration = -duration; }
        }
        if (duration > 0) {
            if (duration > 1) { qWarning() << "Sleep long duration" << duration << "s"; }
            std::unique_lock lock(m_interruptMutex);
            m_interruptCond.wait_for(lock, std::chrono::duration<double>(duration));
        } else {
            qWarning() << "Sleep negative duration" << duration << "s";
        }
    }

    inline bool writeAudio(int batch) {
        for (int i = 0; i < batch && m_audioSink->freeByte() > MAX_AUDIO_FRAME_SIZE; ++i) {
            AudioFrame sample = m_demuxer->getSample(true);
            if (!sample.isValid()) { return false; }
            m_audioSink->write(reinterpret_cast<const char *>(sample.getSampleData()), sample.getDataLen());
            m_demuxer->popSample(true);
        }
        return true;
    }

public:
    Playback(Demuxer *demuxer, QObject *parent): QObject(nullptr), m_demuxer(demuxer) {
        m_affinityThread = new QThread;
        m_affinityThread->setObjectName("PlayThread");
        this->moveToThread(m_affinityThread);
        connect(this, &Playback::startWork, this, &Playback::onWork);
        connect(this, &Playback::stopWork, this, [=] { this->m_audioSink->stop(); });
        connect(this, &Playback::setAudioStartPoint, this, [=](qreal t) {this->m_audioSink->setStartPoint(t);});
        connect(this, &Playback::setAudioVolume, this, [=](qreal volume) {this->m_audioSink->setVolume(volume);});
        connect(this, &Playback::setAudioSpeed, this, [=](qreal speed) {this->m_audioSink->setSpeed(speed);});
        connect(this, &Playback::updateVideoFrame, this, [=]{ emit setPicture(m_demuxer->getPicture(true, false));});
        connect(this, &Playback::clearRingBuffer, this, [=] {this->m_audioSink->clear(); });
        connect(m_affinityThread, &QThread::started, [=]{
            PonyAudioFormat format;
            format.setSampleRate(44100);
            format.setChannelCount(2);
            format.setSampleFormat(PonySampleFormat::Int16);
            this->m_audioSink = new PonyAudioSink(format, MAX_AUDIO_FRAME_SIZE * 2);
        });
        m_affinityThread->start();
    }

    qreal pos() {
        return m_audioSink->getProcessSecs(false);
    }

    virtual ~Playback() {
        m_affinityThread->quit();
    }

    void setVolume(qreal volume) {
        emit setAudioVolume(volume, QPrivateSignal());
    }

    void setSpeed(qreal speed) {
        emit setAudioSpeed(speed, QPrivateSignal());
    }

    void showFrame() {
        emit updateVideoFrame(QPrivateSignal());
    }

    /**
     * 是否正在播放
     * @return 状态
     */
    bool isPlaying() { return m_isPlaying; }

    /**
     * 是否请求停止
     * @return 状态
     */
    bool isInterrupted() { return m_isInterrupt; }

    void setStartPoint(qreal startPoint) {
        m_isInterrupt = false;
        qDebug() << "SetStartPoint" << startPoint;
        emit setAudioStartPoint(startPoint, QPrivateSignal());
    }

    /**
     * 开始进行处理, 发送信号后方法将立即返回.
     */
    void start() {
//        std::unique_lock lock(m_workMutex);
        m_isInterrupt = false;
        emit startWork(QPrivateSignal());
    }

    /**
     * 清空内部缓冲区, 需要保证此刻没有读写操作.
     */
    void clear() {
        emit clearRingBuffer(QPrivateSignal());
    }

    /**
     * 尽快暂停处理, 这个方法将会阻塞直到当前工作停止. 这个方法不会丢失数据.
     */
    void pause() {
        m_isInterrupt = true;
        m_interruptCond.notify_all();
        std::unique_lock lock(m_workMutex);
    }

    /**
     * 立即停止, 清空缓冲区的数据.
     */
    void stop() {
        m_isInterrupt = true;
        m_interruptCond.notify_all();
        std::unique_lock lock(m_workMutex); // make sure stop
        emit stopWork(QPrivateSignal());
        emit setAudioStartPoint(0.0, QPrivateSignal());
        emit clearRingBuffer(QPrivateSignal());
    }

private slots:

    /**
     * 播放音视频. 需要保证 demuxer 可以正常阻塞.
     */
    void onWork() {
        std::unique_lock lock(m_workMutex, std::defer_lock);
        if (!lock.try_lock()) { return; } // not allow neat run
        changeState(true);
        writeAudio(5);
        m_audioSink->start();
        while(!m_isInterrupt) {
            VideoFrame pic = m_demuxer->getPicture();
            if (!pic.isValid()) { emit resourcesEnd(); break; }
            emit setPicture(pic);
            if (!writeAudio(10)) { emit resourcesEnd(); break; }
            VideoFrame next = m_demuxer->getPicture(true, true);
            QCoreApplication::processEvents(); // process setVolume setSpeed etc
            syncTo(next);
        }
        m_audioSink->pause();
        changeState(false);
    };



signals:
    void startWork(QPrivateSignal);
    void stopWork(QPrivateSignal);
    void clearRingBuffer(QPrivateSignal);
    void setAudioStartPoint(qreal startPoint, QPrivateSignal);
    void setAudioVolume(qreal volume, QPrivateSignal);
    void setAudioSpeed(qreal speed, QPrivateSignal);
    void updateVideoFrame(QPrivateSignal);
    void setPicture(VideoFrame pic);
    void stateChanged(bool isPlaying);
    void resourcesEnd();


};

#endif //PONYPLAYER_VIDEOWORKER_H
