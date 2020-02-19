#include "qstring.h"
#include "communication.h"
#include "QtSerialPort/qserialport.h"
#include "qthread.h"
#include "qmutex.h"

communication::communication(QObject *parent) : QObject(parent)
{
    m_opened = false;
    m_busy = false;
    rsp_size = 0;

    m_serial = new QSerialPort(parent);

    m_req_queue = new QQueue<req_param>();

    m_period_timer = new QTimer(this);
    m_period_timer->setInterval(PERIOD_TIMEOUT);

    m_rsp_timer = new QTimer(this);

    m_req_timer = new QTimer(this);
    m_req_timer->setInterval(REQ_TIMEOUT);

    m_frame_timer = new QTimer(this);
    m_frame_timer->setInterval(FRAME_TIMEOUT);

    m_crc = new crc16();

    QObject::connect(m_serial,SIGNAL(readyRead()),this,SLOT(handle_rsp_ready_event()));

    QObject::connect(m_frame_timer,SIGNAL(timeout()),this,SLOT(handle_frame_timeout_event()));
    QObject::connect(m_rsp_timer,SIGNAL(timeout()),this,SLOT(handle_rsp_timeout_event()));
    QObject::connect(m_req_timer,SIGNAL(timeout()),this,SLOT(handle_req_timeout_event()));
    QObject::connect(m_period_timer,SIGNAL(timeout()),this,SLOT(handle_query_weight_timeout_event()));
}



void communication::handle_query_weight_timeout_event()
{
    if (m_req_queue->size() == 0) {
        handle_query_weight_req();
    }

    if (m_busy == false)
    {
        m_busy= true;
        m_req_timer->start();
    }
}

/*处理请求队列*/
void communication::handle_req_timeout_event()
{
    req_param req;

    m_req_timer->stop();

    if (!m_req_queue->isEmpty()) {
        req = m_req_queue->dequeue();
        qDebug("dequeue......");

        req_level = req.level;
        req_code = req.code;
        req_weight = req.value;

        if (m_opened) {
            m_serial->write(req.send,req.size);
            qDebug("start rsp timer.");
            m_rsp_timer->start(req.timeout);

         } else {
             qWarning("串口是关闭的，无法发送数据.");
             handle_rsp_ready_event();
         }

    }

}

/*接收超时*/
void communication::handle_rsp_timeout_event()
{
    rsp_size = 0;
    handle_frame_timeout_event();
}

/*接受完成一帧数据*/
void communication::handle_frame_timeout_event()
{
    int rc = -1;
    uint16_t crc_recv,crc_calculate;

    int weight1 = 0x7fff,weight2=0x7fff,weight3=0x7fff,weight4=0x7fff;
    QString config("获取错误");

    m_rsp_timer->stop();/*停止定时器*/
    m_frame_timer->stop();

    m_busy = false;


    if (rsp_size == 0) {
        rc = -10;
        qWarning("回应超时");
        goto err_exit;
    }

    if (rsp_size < 8) {
        qWarning("回应长度小于8错误");

        rc = -11;/*长度错误*/
        goto err_exit;
    }

    crc_recv = rsp[rsp_size - 1] * 256 + rsp[rsp_size - 2];
    crc_calculate = m_crc->calculate_crc(rsp,rsp_size - 2);
    if (crc_recv != crc_calculate) {
        qWarning("crc recv:%d != %d crc calculate.",crc_recv,crc_calculate);
        rc = -12;/*校验错误*/
        goto err_exit;
    }

    if (req_code != rsp[4]) {
        qWarning("recv code:%d != req code:%d err.",rsp[1],req_code);
        rc = -13;/*操作码错误*/
        goto err_exit;
    }

    switch (req_code ) {
    case REQ_CODE_QUERY_CONFIGRATION:/*配置*/
        if (rsp_size <= 7) {
            config = "获取错误";
        } else {
            config = "";
            for (int i = 5;i < rsp_size - 2;i ++) {
                config +=  (QString::number(rsp[i]) + ((i != rsp_size -3) ? "--" :""));
            }
        }
    break;
    case REQ_CODE_TARE:/*去皮*/
    case REQ_CODE_CALIBRATION_ZERO:/*校准*/
    case REQ_CODE_CALIBRATION_FULL:
        if (rsp_size == 8 ) {
            if (rsp[5] == 0x00) { /*成功*/
                rc = 0;
            } else {
                rc = -1;/*失败*/
            }

        } else {
            rc = -20;/*协议错误*/
        }
    break;
     case REQ_CODE_QUERY_WEIGHT:/*净重*/
    if (rsp_size >= 9) {
            weight1 = rsp[6] * 256 + rsp[5];
            rc = 0;
        if (rsp_size >= 11) {
            weight2 = rsp[8] * 256 + rsp[7];
        }
        if (rsp_size >= 13) {
            weight3 = rsp[10] * 256 + rsp[9];
        }
        if (rsp_size >= 15) {
            weight4 = rsp[12] * 256 + rsp[11];
        }

        } else {
            rc = -20;/*协议错误*/
        }
    break;

    default:
        qWarning("协议错误.");

    }


err_exit:
    /*配置*/
    if (req_code == REQ_CODE_QUERY_CONFIGRATION) {
        emit rsp_query_configration(config);
    } else if (req_code == REQ_CODE_TARE) {/*去皮*/
        emit rsp_tare_result(req_level,rc);
    } else if (req_code == REQ_CODE_CALIBRATION_ZERO ||req_code == REQ_CODE_CALIBRATION_FULL) {
        emit rsp_calibration_result(req_level,req_weight,rc);
    } else if (req_code == REQ_CODE_QUERY_WEIGHT) {
        emit rsp_query_weight_result(rc,weight1,weight2,weight3,weight4);
    }
    qDebug("start period timer.");
    m_period_timer->start();
    rsp_size = 0;
}

/*读到数据*/
void communication::handle_rsp_ready_event()
{

    QByteArray rsp_array;

    rsp_array = m_serial->readAll();

    if (rsp_array.size() + rsp_size >= 100) {
        rsp_size = 0;
    } else {
        memcpy(&rsp[rsp_size],(uint8_t*)rsp_array.data(), rsp_array.size());
        rsp_size += rsp_array.size();
    }

    m_frame_timer->start();
}




/*打开串口*/
void communication::handle_open_serial_port_req(QString port_name,int baudrates,int data_bits,int parity)
{

    bool success;

    m_serial->setPortName(port_name);
    m_serial->setBaudRate(baudrates);
    if (data_bits == 8) {
        m_serial->setDataBits(QSerialPort::Data8);
    } else {
       m_serial->setDataBits(QSerialPort::Data7);
    }

    if (parity == 0){
        m_serial->setParity(QSerialPort::NoParity);
    } else if (parity == 1){
        m_serial->setParity(QSerialPort::OddParity);
    } else  {
         m_serial->setParity(QSerialPort::EvenParity);
    }

    success = m_serial->open(QSerialPort::ReadWrite);
    if (success) {
        qDebug("打开串口成功.");
        m_opened = true;

        m_req_timer->start();/*开始请求*/
        m_period_timer->start();

        emit rsp_open_serial_port_result(0);/*发送成功信号*/
    } else {
        qWarning("打开串口失败.");
        emit rsp_open_serial_port_result(-1);/*发送失败信号*/
    }
}


/*关闭串口*/
void communication::handle_close_serial_port_req(QString port_name)
{
    (void) port_name;

    m_serial->flush();

    m_serial->close();

    m_period_timer->stop();
    m_req_timer->stop();
    m_rsp_timer->stop();
    m_frame_timer->stop();

    m_opened = false;

    emit rsp_close_serial_port_result(0);/*发送关闭串口成功信号*/
}

/*轮询净重值*/
void communication::handle_query_weight_req()
{
    req_param query_weight;

    uint16_t crc16;

    if (m_opened == false){
        return;
    }

    query_weight.timeout = QUERY_WEIGHT_TIMEOUT;

    query_weight.code = REQ_CODE_QUERY_WEIGHT;

    query_weight.size = 8;

    query_weight.send[0] = 0x4d;

    query_weight.send[1] = 0x4c;

    query_weight.send[2] = 3;
    query_weight.send[3] = 1;
    query_weight.send[4] = query_weight.code;
    query_weight.send[5] = 0;


    crc16 = m_crc->calculate_crc((uint8_t *)query_weight.send,query_weight.size - 2);
    query_weight.send[6] = (crc16 & 0xFF);
    query_weight.send[7] =  (crc16 >> 8);

    m_req_queue->enqueue(query_weight);

    qDebug("enqueue query weight req. queue size:%d.",m_req_queue->size());

}


/*去皮*/
void communication::handle_tare_req(int level)
{
    req_param tare;

    uint16_t crc16;

    if (m_opened == false){
        return;
    }

    tare.timeout = REMOVE_TARE_TIMEOUT;
    tare.level = level;
    tare.code = REQ_CODE_TARE;
    tare.size = 8;

    tare.send[0] = 0x4d;
    tare.send[1] = 0x4c;
    tare.send[2] = 3;
    tare.send[3] = 1;/*主控板地址*/
    tare.send[4] = tare.code;/*操作码*/
    tare.send[5] = (uint8_t)(tare.level * 10 + 1);/*称号*/

    crc16 = m_crc->calculate_crc((uint8_t *)tare.send,tare.size - 2);
    tare.send[6] = (crc16 & 0xFF);
    tare.send[7] = (crc16 >> 8);

    m_req_queue->enqueue(tare);

    if (m_busy == false)
    {
        m_busy= true;
        m_req_timer->start();
    }
     qDebug("tare queue size:%d.",m_req_queue->size());
}


/*校准*/
void communication::handle_calibration_req(int level,int calibration_weight)
{
    req_param calibration;

    uint16_t crc16;

    if (m_opened == false){
        return;
    }

    calibration.timeout = CALIBRATION_TIMEOUT;
    calibration.level = level;

    if (calibration_weight == 0) {
        calibration.code = REQ_CODE_CALIBRATION_ZERO;
    } else {
        calibration.code = REQ_CODE_CALIBRATION_FULL;
    }
    calibration.value = calibration_weight;


    calibration.size = 10;

    calibration.send[0] = 0x4d;
    calibration.send[1] = 0x4c;
    calibration.send[2] = 5;/*长度*/
    calibration.send[3] = 1;/*主控板地址*/
    calibration.send[4] = calibration.code;/*操作码*/
    calibration.send[5] = (uint8_t)(level * 10 + 1);/*称号*/

    calibration.send[6] =  calibration_weight & 0xFF;
    calibration.send[7] = calibration_weight >> 8;



    crc16 = m_crc->calculate_crc((uint8_t *)calibration.send,calibration.size - 2);
    calibration.send[8] = (crc16 & 0xFF);
    calibration.send[9] = (crc16 >> 8);

    m_req_queue->enqueue(calibration);
    if (m_busy == false)
    {
        m_busy= true;
        m_req_timer->start();
    }
     qDebug("cal queue size:%d.",m_req_queue->size());
}


/*轮询配置*/
void communication::handle_query_configration_req()
{
    req_param query_configration;

    uint16_t crc16;

    if (m_opened == false){
        return;
    }

    query_configration.timeout = QUERY_CONFIGRATION_TIMEOUT;

    query_configration.code = REQ_CODE_QUERY_CONFIGRATION;

    query_configration.size = 7;

    query_configration.send[0] = 0x4d;

    query_configration.send[1] = 0x4c;

    query_configration.send[2] = 2;
    query_configration.send[3] = 1;
    query_configration.send[4] = query_configration.code;



    crc16 = m_crc->calculate_crc((uint8_t *)query_configration.send,query_configration.size - 2);
    query_configration.send[5] = (crc16 & 0xFF);
    query_configration.send[6] =  (crc16 >> 8);

    m_req_queue->enqueue(query_configration);

    qDebug("enqueue query configration req. queue size:%d.",m_req_queue->size());

}
