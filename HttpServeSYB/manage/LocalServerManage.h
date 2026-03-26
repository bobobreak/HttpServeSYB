#pragma once

#include <QLocalServer>
#include <QLocalSocket>
#include "AbstractManage.h"

class LocalServerManage  : public AbstractManage
{
	Q_OBJECT
	Q_DISABLE_COPY(LocalServerManage)

public:

	LocalServerManage(const ServerConfig config = {}, QObject* parent = nullptr);
	virtual ~LocalServerManage();

	bool listen(const QString& name);

private:
	bool isRunning() override;
	bool OnStart() override;
	bool OnFinish() override;

private:
	QPointer<QLocalServer> m_localServer;
	QString m_listenName;
};

