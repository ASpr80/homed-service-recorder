#ifndef CONTROLLER_H
#define CONTROLLER_H

#define SERVICE_VERSION     "1.0.6"

#include "database.h"
#include "homed.h"

class DeviceObject;
typedef QSharedPointer <DeviceObject> Device;

class DeviceObject
{

public:

    DeviceObject(const QString &key, const QString &topic) :
        m_key(key), m_topic(topic), m_available(true) {}

    inline QString key(void) { return m_key; }

    inline QString topic(void) { return m_topic; }
    inline void setTopic(const QString &value) { m_topic = value; }

    inline bool available(void) { return m_available; }
    inline void setAvailable(bool value) { m_available = value; }

private:

    QString m_key, m_topic;
    bool m_available;

};

class Controller : public HOMEd
{
    Q_OBJECT

public:

    Controller(const QString &configFile);

    enum class Command
    {
        restartService,
        updateItem,
        removeItem,
        getData
    };

    Q_ENUM(Command)

private:
    
    struct DeviceStruct
    {
        QString name;
        bool available;
    };

    Database *m_database;
    QMetaEnum m_commands;

    QList <QString> m_types;
    QMap <QString, Device> m_devices;

    Device findDevice(const QString &search);
    void publishItems(void);

private slots:

    void mqttConnected(void) override;
    void mqttReceived(const QByteArray &message, const QMqttTopicName &topic) override;

    void itemAdded(const Item &item);

};

#endif
