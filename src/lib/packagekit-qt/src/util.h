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

#ifndef UTIL_H
#define UTIL_H

#include <QtCore>
#include "client.h"

namespace PackageKit {

class Package;

class Util
{

public:
	/**
	 * Converts \p str from the PK naming scheme to the QPackageKit enum naming
	 * scheme, prepending \p prefix to the result, and returns the value for enum \p enum
	 * For example, enumFromString("get_requires", "Action") == "ActionGetRequires"
	 * \return the enum value
	 */
	template<class T> static int enumFromString(const QString& str, const char* enumName, const QString& prefix = QString())
	{
		QString realName;
		bool lastWasDash = false;
		QChar buf;

		for(int i = 0 ; i < str.length() ; ++i) {
			buf = str[i].toLower();
			if(i == 0 || lastWasDash) {
				buf = buf.toUpper();
			}

			lastWasDash = false;
			if(buf.toAscii() == '-') {
				lastWasDash = true;
			} else if(buf.toAscii() == '~') {
				lastWasDash = true;
				realName += "Not";
			} else {
				realName += buf;
			}
		};

		if(!prefix.isNull())
			realName = prefix + realName;

		// Filter quirk
		if(enumName == QString("Filter")) {
			if(realName == QString("FilterNone"))
				realName = "NoFilter";

			if(realName == QString("FilterDevel") || realName == QString("FilterNotDevel"))
				realName += "opment";
		}

		// Action quirk
		if(enumName == QString("Action") && realName == QString("ActionUpdatePackage"))
			realName = "ActionUpdatePackages";


		int id = T::staticMetaObject.indexOfEnumerator(enumName);
		QMetaEnum e = T::staticMetaObject.enumerator(id);
		int enumValue = e.keyToValue(realName.toAscii().data());

		if(enumValue == -1) {
			enumValue = e.keyToValue(QString("Unknown").append(enumName).toAscii().data());
			qDebug() << "enumFromString (" << enumName << ") : converted" << str << "to" << QString("Unknown").append(enumName) << ", enum value" << enumValue;
		}
		return enumValue;
	}

	template<class T> static QString enumToString(int value, const char* enumName, const QString& prefix = QString())
	{
		int id = T::staticMetaObject.indexOfEnumerator(enumName);
		QMetaEnum e = T::staticMetaObject.enumerator(id);
		QString enumString = e.valueToKey(value);
		if(enumString.isNull()) {
			qDebug() << "Enum key not found while searching for value" << value << "in enum" << enumName;
		}

		// Remove the prefix
		if(!prefix.isNull() && enumString.indexOf(prefix) == 0)
			enumString.remove(0, prefix.length());

		QString pkName;
		for(int i = 0 ; i < enumString.length() - 1 ; ++i) {
			pkName += enumString[i];
			if(enumString[i+1].isUpper())
				pkName += QChar('-');
		}
		pkName += enumString[enumString.length() - 1];

		if(enumName == QString("Filter")) {
			if(pkName.indexOf("Not-") == 0) {
				pkName = pkName.replace(0, 4, "~");
			}
			if(pkName == "No-Filter")
				pkName = "none";
			if(pkName == "Development")
				pkName = "devel";
			if(pkName == "~Development")
				pkName = "~devel";
		}

		return pkName.toLower();
	};

	static QStringList packageListToPids(const QList<Package*>& packages);

	static QString filtersToString(const QFlags<PackageKit::Client::Filter>& flags);

	/*
	 * Describes the different errors that might happen on the bus
	 *
	 * \sa Client::error
	 * \sa Transaction::error
	 */
	typedef enum {
		NoError = 0,
		ErrorFailed,
		ErrorFailedAuth,
		ErrorNoTid,
		ErrorAlreadyTid,
		ErrorRoleUnkown,
		ErrorCannotStartDaemon,
		ErrorInvalidInput,
		ErrorInvalidFile,
		ErrorNotSupported
	} TransactionError;

	static Client::DaemonError errorFromString (QString errorName);

};

} // End namespace PackageKit

#endif
