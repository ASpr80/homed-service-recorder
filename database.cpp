#include "database.h"
#include "logger.h"

bool ItemObject::skip(qint64 timestamp, double value)
{
    if (timestamp < m_timestamp + m_debounce)
    {
        double check = m_value.toDouble();
        return (check < value && check + m_threshold > value) || (check > value && check - m_threshold < value);
    }

    return false;
}

Database::Database(QSettings *config, QObject *parent) : QObject(parent), m_timer(new QTimer(this)), m_db(QSqlDatabase::addDatabase("QSQLITE", "db"))
{
    QSqlQuery query(m_db);

    m_db.setDatabaseName(config->value("database/file", "/opt/homed-recorder/homed-recorder.db").toString());
    m_days = static_cast <quint16> (config->value("database/days", 7).toInt());
    m_trigger = {"action", "event", "scene"};

    logInfo << "Using database" << m_db.databaseName() << "with" << m_days << "days purge inerval";

    if (!m_db.open())
    {
        logWarning << "Database open error";
        return;
    }

    query.exec("CREATE TABLE IF NOT EXISTS item (id INTEGER PRIMARY KEY AUTOINCREMENT, endpoint TEXT NOT NULL, property TEXT NOT NULL, debounce INTEGER NOT NULL, threshold REAL NOT NULL)");
    query.exec("CREATE TABLE IF NOT EXISTS data (id INTEGER PRIMARY KEY AUTOINCREMENT, item_id INTEGER REFERENCES item(id) ON DELETE CASCADE, timestamp INTEGER NOT NULL, value TEXT NOT NULL)");
    query.exec("CREATE UNIQUE INDEX item_index ON item (endpoint, property)");

    query.exec("PRAGMA foreign_keys = ON");
    query.exec("SELECT * FROM item");

    while (query.next())
    {
        Item item(new ItemObject(static_cast <quint32> (query.value(0).toInt()), query.value(1).toString(), query.value(2).toString(), static_cast <quint32> (query.value(3).toInt()), query.value(4).toDouble()));
        m_items.insert(QString("%1/%2").arg(item->endpoint(), item->property()), item);
    }

    connect(m_timer, &QTimer::timeout, this, &Database::update);
    m_timer->start(1000);
}

Database::~Database(void)
{
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase("db");
}

bool Database::updateItem(const QString &endpoint, const QString &property, quint32 debounce, double threshold)
{
    QString key = QString("%1/%2").arg(endpoint, property);
    QSqlQuery query(m_db);

    if (m_items.contains(key))
    {
        const Item &item = m_items.value(key);

        if (!query.exec(QString("UPDATE item SET debounce = %1, threshold = %2 WHERE id = %3").arg(debounce).arg(threshold).arg(item->id())))
            return false;

        item->setDebounce(debounce);
        item->setThreshold(threshold);
    }
    else
    {
        if (!query.exec(QString("INSERT INTO item (endpoint, property, debounce, threshold) VALUES ('%1', '%2', %3, %4)").arg(endpoint, property).arg(debounce).arg(threshold)))
            return false;

        m_items.insert(key, Item(new ItemObject(static_cast <qint32> (query.lastInsertId().toInt()), endpoint, property, debounce, threshold)));
    }

    return true;
}

bool Database::removeItem(const QString &endpoint, const QString &property)
{
    auto it = m_items.find(QString("%1/%2").arg(endpoint, property));
    QSqlQuery query(m_db);

    if (it == m_items.end() || !query.exec(QString("DELETE FROM item WHERE id = %1").arg(it.value()->id())))
        return false;

    m_items.erase(it);
    return true;
}

void Database::insertData(const Item &item, const QString &value)
{
    qint64 timestamp = QDateTime::currentMSecsSinceEpoch();

    if (!item->timestamp())
    {
        QSqlQuery query(QString("SELECT timestamp, value FROM data WHERE item_id = %1 ORDER BY id DESC LIMIT 1").arg(item->id()), m_db);

        if (query.first())
        {
            item->setTimestamp(query.value(0).toLongLong());
            item->setValue(query.value(1).toString());
        }
    }

    if ((item->value() == value && !m_trigger.contains(item->property())) || item->skip(timestamp, value.toDouble()))
        return;

    logInfo << "insert" << item->id() << item->endpoint() << item->property() << value;
    m_queue.enqueue({item->id(), timestamp, value});

    item->setTimestamp(timestamp);
    item->setValue(value);
}

void Database::getData(const Item &item, qint64 start, qint64 end, QList<Record> &list)
{
    QString queryString = QString("SELECT timestamp, value FROM data WHERE item_id = %1").arg(item->id());
    QSqlQuery query(m_db);

    if (start)
    {
        query.exec(QString(queryString).append(" AND timestamp <= %1 ORDER BY id DESC LIMIT 1").arg(start));
        queryString.append(QString(" AND timestamp > %1").arg(start));
    }

    if (query.first())
        list.append({item->id(), query.value(0).toLongLong(), query.value(1).toString()});

    if (end)
        queryString.append(QString(" AND timestamp <= %1").arg(end));

    query.exec(queryString);

    while (query.next())
        list.append({item->id(), query.value(0).toLongLong(), query.value(1).toString()});
}

void Database::update(void)
{
    quint64 timestamp = QDateTime::currentSecsSinceEpoch();
    QSqlQuery query(m_db);

    query.exec("BEGIN TRANSACTION");

    while (!m_queue.isEmpty())
    {
        Record record = m_queue.dequeue();
        query.exec(QString("INSERT INTO data (item_id, timestamp, value) VALUES (%1, %2, '%3')").arg(record.id).arg(record.timestamp).arg(record.value));
    }

    query.exec("COMMIT");

    if (!m_days || timestamp % 3600 || QDateTime::currentDateTime().time().hour())
        return;

    query.exec(QString("DELETE FROM data WHERE timestamp < %1").arg((timestamp - m_days * 86400) * 1000));
    query.exec("VACUUM");
}
