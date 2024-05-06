#include "controller.h"
#include "logger.h"

Controller::Controller(const QString &configFile) : HOMEd(configFile), m_database(new Database(getConfig(), this)), m_commands(QMetaEnum::fromType <Command> ())
{
    logInfo << "Starting version" << SERVICE_VERSION;
    logInfo << "Configuration file is" << getConfig()->fileName();

    m_services = {"zigbee", "modbus", "custom"};
}

QString Controller::endpointName(const QString &endpoint)
{
    QList <QString> search = endpoint.split('/');

    for (auto it = m_devices.begin(); it != m_devices.end(); it++)
    {
        QList <QString> list = it.key().split('/');

        if (list.value(0) != search.value(0))
            continue;

        if (list.value(1) == search.value(1))
            return endpoint;

        if (it.value() == search.value(1))
        {
            search.replace(1, list.value(1));
            return search.join('/');
        }
    }

    return QString();
}

void Controller::publishItems(void)
{
    QJsonArray items;

    for (auto it = m_database->items().begin(); it != m_database->items().end(); it++)
        items.append(QJsonObject {{"endpoint", it.value()->endpoint()}, {"property", it.value()->property()}, {"debounce", QJsonValue::fromVariant(it.value()->debounce())}, {"threshold", it.value()->threshold()}});

    mqttPublish(mqttTopic("status/recorder"), {{"items", items}, {"timestamp", QDateTime::currentSecsSinceEpoch()}, {"version", SERVICE_VERSION}}, true);
}

void Controller::mqttConnected(void)
{
    mqttSubscribe(mqttTopic("command/recorder"));
    mqttSubscribe(mqttTopic("status/#"));

    m_devices.clear();
    publishItems();

    mqttPublishStatus();
}

void Controller::mqttReceived(const QByteArray &message, const QMqttTopicName &topic)
{
    QString subTopic = topic.name().replace(mqttTopic(), QString());
    QJsonObject json = QJsonDocument::fromJson(message).object();

    if (subTopic == "command/recorder") // TODO: publish events
    {
        switch (static_cast <Command> (m_commands.keyToValue(json.value("action").toString().toUtf8().constData())))
        {
            case Command::restartService:
            {
                logWarning << "Restart request received...";
                mqttPublish(mqttTopic("command/recorder"), QJsonObject(), true);
                QCoreApplication::exit(EXIT_RESTART);
                break;
            }
            case Command::updateItem:
            {
                if (!m_database->updateItem(json.value("endpoint").toString(), json.value("property").toString(), static_cast <quint32> (json.value("debounce").toInt()), json.value("threshold").toDouble()))
                {
                    logWarning << "update item request failed";
                    break;
                }

                publishItems();
                break;
            }
            case Command::removeItem:
            {
                if (!m_database->removeItem(json.value("endpoint").toString(), json.value("property").toString()))
                {

                    logWarning << "remove item request failed";
                    break;
                }

                publishItems();
                break;
            }
            case Command::getData:
            {
                const Item &item = m_database->items().value(QString("%1/%2").arg(json.value("endpoint").toString(), json.value("property").toString()));
                QList <Database::Record> list;
                QJsonArray timestamps, values;
                qint64 time = QDateTime::currentMSecsSinceEpoch();

                if (!item.isNull())
                    m_database->getData(item, json.value("start").toVariant().toLongLong(), json.value("end").toVariant().toLongLong(), list);

                for (int i = 0; i < list.count(); i++)
                {
                    const Database::Record &record = list.at(i);
                    timestamps.append(record.timestamp);
                    values.append(record.value);
                }

                mqttPublish(mqttTopic("recorder"), {{"id", json.value("id").toString()}, {"time", QDateTime::currentMSecsSinceEpoch() - time}, {"timestamps", timestamps}, {"values", values}});
                break;
            }
        }
    }
    else if (subTopic.startsWith("status/"))
    {
        QString service = subTopic.split('/').value(1);
        QJsonArray devices = json.value("devices").toArray();
        bool names = json.value("names").toBool();

        if (!m_services.contains(service))
            return;

        for (auto it = devices.begin(); it != devices.end(); it++)
        {
            QJsonObject device = it->toObject();
            QString name = device.value("name").toString(), id, key, item;

            if (device.value("removed").toBool())
                continue;

            switch (m_services.indexOf(service))
            {
                case 0: id = device.value("ieeeAddress").toString(); break; // zigbee
                case 1: id = QString("%1.%2").arg(device.value("portId").toInt()).arg(device.value("slaveId").toInt()); break; // modbus
                case 2: id = device.value("id").toString(); break; // custom
            }

            if (name.isEmpty())
                name = id;

            key = QString("%1/%2").arg(service, id);
            item =  names ? name : id;

            if (m_devices.contains(key) && m_devices.value(key) != name)
            {
                mqttUnsubscribe(mqttTopic("device/%1/%2").arg(service, item));
                mqttUnsubscribe(mqttTopic("fd/%1/%2").arg(service, item));
                mqttUnsubscribe(mqttTopic("fd/%1/%2/#").arg(service, item));
                m_devices.remove(key);
            }

            if (!m_devices.contains(key))
            {
                mqttSubscribe(mqttTopic("device/%1/%2").arg(service, item));
                mqttSubscribe(mqttTopic("fd/%1/%2").arg(service, item));
                mqttSubscribe(mqttTopic("fd/%1/%2/#").arg(service, item));
                m_devices.insert(key, name);
            }
        }
    }
    else if (subTopic.startsWith("device/"))
    {
        QString endpoint = endpointName(subTopic.mid(subTopic.indexOf('/') + 1));

        if (endpoint.isEmpty() || json.value("status").toString() == "online")
            return;

        for (auto it = m_database->items().begin(); it != m_database->items().end(); it++)
        {
            if (!it.key().startsWith(endpoint))
                continue;

            m_database->insertData(it.value(), UNAVAILABLE_STRING);
        }
    }
    else if (subTopic.startsWith("fd/"))
    {
        QString endpoint = endpointName(subTopic.mid(subTopic.indexOf('/') + 1));

        if (endpoint.isEmpty())
            return;

        for (auto it = json.begin(); it != json.end(); it++)
        {
            const Item &item = m_database->items().value(QString("%1/%2").arg(endpoint, it.key()));

            if (item.isNull())
                continue;

            m_database->insertData(item, it.value().toVariant().toString());
        }
    }
}
