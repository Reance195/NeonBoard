#pragma once

#include <initializer_list>

class QQmlContext;
class QObject;

struct NamedObject { const char *name; QObject *object; };

// QML 上下文属性一张表注入，避免 AppMain.cpp 里 40+ 行逐条 setContextProperty。
void applyContextProperties(QQmlContext *context,
                            std::initializer_list<NamedObject> objects);
