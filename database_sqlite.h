#ifndef DATABASE_SQLITE_H
#define DATABASE_SQLITE_H

#include <QSqlDatabase>

#include "database_interface.h"

class DatabaseSQLite final: public DatabaseInterface
{
public:
    DatabaseSQLite(QString const & database_name);

public:
    auto add(QString name, Feature feature) -> bool override;


private:
    auto createTable() -> bool;
    auto load() -> bool;

private:
    QSqlDatabase database_;
};

#endif // DATABASE_SQLITE_H
