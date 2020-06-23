#ifndef DATABASE_MYSQL_H
#define DATABASE_MYSQL_H

#include <QSqlDatabase>

#include "database_interface.h"

class DatabaseMySQL final: public DatabaseInterface
{
public:
    DatabaseMySQL(
        QString const & host_name,
        QString const & user_name,
        QString const & password,
        QString const & database_name
    );

public:
    auto add(QString name, Feature feature) -> bool override;

private:
    auto createTable() -> bool;
    auto load() -> bool;

private:
    QSqlDatabase database_;
};

#endif // DATABASE_MYSQL_H
