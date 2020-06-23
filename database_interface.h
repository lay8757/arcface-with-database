#ifndef DATABASE_INTERFACE_H
#define DATABASE_INTERFACE_H

#include <vector>

#include <QString>

class DatabaseInterface
{
public:
    using Feature = std::vector<uint8_t>;
    using Features = std::vector<std::pair<Feature, QString>>;

public:
    DatabaseInterface();
    virtual ~DatabaseInterface();

public:
    virtual auto add(QString name, Feature feature) -> bool = 0;

public:
    auto features() const noexcept -> Features const & { return features_; }

protected:
    Features features_;
};

#endif // DATABASE_INTERFACE_H
