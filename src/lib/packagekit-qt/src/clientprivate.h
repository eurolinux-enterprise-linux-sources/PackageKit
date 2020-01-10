/*
 * This file is part of the QPackageKit project
 * Copyright (C) 2008 Adrien Bustany <madcat@mymadcat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB. If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef CLIENTPRIVATE_H
#define CLIENTPRIVATE_H

#include <QtCore>
#include <QtSql>
#include "client.h"

namespace PackageKit {

class Client;
class DaemonProxy;
class Transaction;

class ClientPrivate : public QObject
{
	Q_OBJECT

public:
	~ClientPrivate();

	DaemonProxy* daemon;
	Client* c;

	QString locale;
	QStringList hints;

	QMutex runningTransactionsLocker;
	QHash<QString, Transaction*> runningTransactions;

	// Get a tid, creates a new transaction and sets it up (ie call SetLocale)
	Transaction* createNewTransaction();

	Client::DaemonError error;

public slots:
	// org.freedesktop.PackageKit
	void transactionListChanged(const QStringList& tids);
	// locked
	void networkStateChanged(const QString& state);
	// restartScheduled
	// repoListChanged
	// updatesChanged
	void serviceOwnerChanged (const QString&, const QString&, const QString&);

private:
	friend class Client;
	ClientPrivate(Client* parent);

private slots:
	void removeTransactionFromPool(const QString& tid);
};

} // End namespace PackageKit

#endif
