#ifndef APPBINDING_H
#define APPBINDING_H
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>

struct AppBinding {
    QString packageName;
    QString label;
    bool isEmpty() const { return packageName.isEmpty(); }
    static bool validPackage(const QString &value) {
        static const QRegularExpression pattern(QStringLiteral("^[A-Za-z][A-Za-z0-9_]*(?:\\.[A-Za-z0-9_]+)+$"));
        return value.size() <= 255 && pattern.match(value).hasMatch();
    }
    QJsonObject json() const { return {{"version", 1}, {"packageName", packageName}, {"label", label}}; }
    static bool parse(const QJsonValue &value, AppBinding *result) {
        *result = AppBinding();
        if (value.isUndefined()) return true;
        if (!value.isObject()) return false;
        const auto object = value.toObject();
        if (object.value("version").toDouble() != 1 || !validPackage(object.value("packageName").toString())
            || !object.value("label").isString() || object.value("label").toString().size() > 160) return false;
        result->packageName = object.value("packageName").toString();
        result->label = object.value("label").toString();
        return true;
    }
    static bool load(const QString &path, AppBinding *result) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 64LL * 1024 * 1024) return false;
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(file.readAll(), &error);
        return error.error == QJsonParseError::NoError && document.isObject()
            && parse(document.object().value("applicationBinding"), result);
    }
    static bool save(const QString &path, const AppBinding &binding) {
        if (!binding.isEmpty() && (!validPackage(binding.packageName) || binding.label.size() > 160)) return false;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 64LL * 1024 * 1024) return false;
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(file.readAll(), &error);
        file.close();
        if (error.error != QJsonParseError::NoError || !document.isObject()) return false;
        auto object = document.object();
        if (binding.isEmpty()) object.remove("applicationBinding");
        else object["applicationBinding"] = binding.json();
        const auto data = QJsonDocument(object).toJson(QJsonDocument::Compact);
        QSaveFile output(path);
        return output.open(QIODevice::WriteOnly) && output.write(data) == data.size() && output.commit();
    }
};
#endif
