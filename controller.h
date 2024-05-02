#ifndef CONTROLLER_H
#define CONTROLLER_H

#define SERVICE_VERSION     "1.0.0"

#include "database.h"
#include "homed.h"

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
    
    Database *m_database;
    QMetaEnum m_commands;

    QList <QString> m_services;
    QMap <QString, QString> m_devices;

    QString endpointName(const QString &endpoint);
    void publishItems(void);

private slots:

    void mqttConnected(void) override;
    void mqttReceived(const QByteArray &message, const QMqttTopicName &topic) override;

};

#endif
