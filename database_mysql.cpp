#include "database_mysql.h"

#include <QSqlQuery>
#include <QSqlError>

#include <QDebug>

DatabaseMySQL::DatabaseMySQL(
    QString const & host_name,
    QString const & user_name,
    QString const & password,
    QString const & database_name
)
{
    qDebug() << QSqlDatabase::drivers();

    database_ = QSqlDatabase::addDatabase("QMYSQL", database_name);
    database_.setHostName(host_name);
    database_.setUserName(user_name);
    database_.setPassword(password);
    database_.setDatabaseName(database_name);
    database_.open();

    if (!database_.isOpen())
    {
        throw std::runtime_error(
            "Failed to open databse " + database_name.toStdString() +
            ": " + database_.lastError().text().toStdString()
        );
    }

    this->createTable();
    this->load();
}

auto DatabaseMySQL::createTable() -> bool
{
    auto query = QSqlQuery(database_);

    auto const success = query.exec(
        "CREATE TABLE IF NOT EXISTS features("            "\n"
        "    id      INTEGER PRIMARY KEY AUTO_INCREMENT," "\n"
        "    name    VARCHAR(32),"                        "\n"
        "    feature BLOB"                                "\n"
        ");"
    );
    if (!success)
    {
        throw std::runtime_error("Failed to exec: " + query.lastError().text().toStdString());
    }
    return success;
}

auto DatabaseMySQL::load() -> bool
{
    auto query = QSqlQuery(database_);

    auto const success = query.exec(
        "SELECT name, feature FROM features"
    );
    if (!success)
    {
        throw std::runtime_error("Failed to exec: " + query.lastError().text().toStdString());
    }

    while (query.next())
    {
        auto name = query.value(QStringLiteral(u"name")).toString();
        auto feature = query.value(QStringLiteral(u"feature")).toByteArray();
        if (name.isEmpty() || feature.size() != 1032)
        {
            qDebug() << "Skip line:" << name << "," << feature.size();
            continue;
        }
        features_.emplace_back(
            Feature(feature.cbegin(), feature.cend()),
            std::move(name)
        );
    }

    qDebug() << "Find" << features_.size() << "faces:";
    {
        auto out = qDebug();
        for (auto const & info: features_)
        {
            out << info.second;
        }
    }
    return true;
}

auto DatabaseMySQL::add(
    QString name,
    Feature feature
) -> bool
{
    assert(!feature.empty());

//    if (!database_.transaction())
//    {
//        qDebug() << "Failed to start transaction:" << database_.lastError().text();
//        return false;
//    }

    auto query = QSqlQuery(database_);
    query.prepare(
        "INSERT INTO features(name, feature)VALUES(:name, :feature);"
    );

    auto feature_bytes = QByteArray(
        reinterpret_cast<char *>(&feature[0]),
        static_cast<int>(feature.size())
    );

    query.bindValue(":name", name);
    query.bindValue(":feature", feature_bytes);
    if (!query.exec() /*|| !database_.commit()*/)
    {
        qDebug() << "Failed to exec"/*" or commit"*/":" << database_.lastError().text();
//        database_.rollback();
        return false;
    }

    features_.emplace_back(std::move(feature), std::move(name));
    return true;
}
