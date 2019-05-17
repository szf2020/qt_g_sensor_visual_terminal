#ifndef COMMUNICATION_H
#define COMMUNICATION_H

#include <QObject>
#include <QtSerialPort/QSerialPort>
#include <QTimer>
#include <QMutex>
#include "crc16.h"
#include "qqueue.h"
#include "req_param.h"


class communication : public QObject
{
    Q_OBJECT
public:
    explicit communication(QObject *parent = nullptr);
    QSerialPort *m_serial;
    enum {
        FRAME_TIMEOUT = 5,
        REQ_TIMEOUT = 5,
        QUERY_WEIGHT_TIMEOUT = 50,
        QUERY_CONFIGRATION_TIMEOUT = 50,
        REMOVE_TARE_TIMEOUT = 300,
        CALIBRATION_TIMEOUT = 300,
        PERIOD_TIMEOUT = 5

    };

    enum {
        REQ_CODE_QUERY_CONFIGRATION = 0,
        REQ_CODE_QUERY_WEIGHT = 1,
        REQ_CODE_TARE = 2,
        REQ_CODE_CALIBRATION_ZERO = 3,
        REQ_CODE_CALIBRATION_FULL = 4,
    };
    void handle_query_weight_req(void);

public slots:
    void handle_open_serial_port_req(QString port_name,int baudrates,int data_bits,int parity);
    void handle_close_serial_port_req(QString port_name);

    void handle_req_timeout_event(void);
    void handle_rsp_timeout_event(void);
    void handle_query_weight_timeout_event(void);
    void handle_frame_timeout_event();

    void handle_rsp_ready_event(void);

    void handle_tare_req(int);
    void handle_calibration_req(int,int);
    void handle_query_configration_req();
signals:

    void rsp_open_serial_port_result(int result);
    void rsp_close_serial_port_result(int result);

    void rsp_query_weight_result(int result,int weight1,int weight2,int weight3,int weight4);
    void rsp_tare_result(int level,int result);
    void rsp_calibration_result(int level,int calibration_weight,int result);
    void rsp_query_configration(QString);

private:
    QQueue<req_param> *m_req_queue;
    QTimer *m_req_timer;
    QTimer *m_rsp_timer;
    QTimer *m_period_timer;
    QTimer *m_frame_timer;


    bool   m_opened;
    crc16 *m_crc;
    bool   m_busy;

    int req_level;
    int req_code;
    int req_weight;
    uint8_t rsp[100];
    int rsp_size;

};

#endif // COMMUNICATION_H
