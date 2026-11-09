#include "AppContext.h"

#include <QObject>
#include <QQmlContext>
#include <QString>

void applyContextProperties(QQmlContext *context,
                            std::initializer_list<NamedObject> objects)
{
    for (const NamedObject &entry : objects)
        context->setContextProperty(QString::fromLatin1(entry.name), entry.object);
}
