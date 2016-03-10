/*
 * Stellarium
 * Copyright (C) 2014-2016 Marcos Cardinot
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Suite 500, Boston, MA  02110-1335, USA.
 */

#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPixmap>
#include <QSettings>
#include <QStringBuilder>
#include <qzipreader.h>

#include "LandscapeMgr.hpp"
#include "StelApp.hpp"
#include "StelAddOnMgr.hpp"
#include "StelFileMgr.hpp"
#include "StelModuleMgr.hpp"

StelAddOnMgr::StelAddOnMgr()
	: m_pConfig(StelApp::getInstance().getSettings())
	, m_pDownloadMgr(new DownloadMgr(this))
	, m_lastUpdate(QDateTime::fromString("2016.01.01", "yyyy.MM.dd"))
	, m_eUpdateFrequency(EVERY_THREE_DAYS)
{
	QDir dir(StelFileMgr::getAddonDir());
	m_sAddonJsonPath = dir.filePath(StelUtils::getApplicationSeries() % "/addons.json");
	m_sInstalledAddonsJsonPath = dir.filePath("installed_addons.json");
	m_sUserAddonJsonPath = dir.filePath("user_addons.json");

	// load settings from config file
	loadConfig();

	// loading json files
	reloadCatalogues();
	if (m_addonsAvailable.isEmpty())
	{
		restoreDefaultAddonJsonFile();
	}
}

StelAddOnMgr::~StelAddOnMgr()
{
}

void StelAddOnMgr::reloadCatalogues()
{
	// clear all hashes
	m_addonsInstalled.clear();
	m_addonsAvailable.clear();
	m_addonsToUpdate.clear();

	// load catalog of installed addons (~/.stellarium/installed_addons.json)
	m_addonsInstalled = loadAddonCatalog(m_sInstalledAddonsJsonPath);

	// load oficial catalog ~/.stellarium/addon_x.x.json
	m_addonsAvailable = loadAddonCatalog(m_sAddonJsonPath);

	// load user catalog ~/.stellarium/user_addon_x.x.json
	m_addonsAvailable.unite(loadAddonCatalog(m_sUserAddonJsonPath));

	// removing the installed addons from 'm_addonsAvailable' hash
	QHashIterator<QString, AddOn*> i(m_addonsInstalled);
	while (i.hasNext())
	{
		i.next();
		QString addonId = i.key();
		AddOn* addonInstalled = i.value();
		AddOn* addonAvailable = m_addonsAvailable.value(addonId);
		if (!addonAvailable)
		{
			continue;
		}
		else if (addonInstalled->getChecksum() == addonAvailable->getChecksum() ||
			 addonInstalled->getDate() >= addonAvailable->getDate())
		{
			m_addonsAvailable.remove(addonId);
		}
		else if (addonInstalled->getDate() < addonAvailable->getDate())
		{
			m_addonsAvailable.remove(addonId);
			m_addonsToUpdate.insert(addonId, addonAvailable);
		}
	}

	emit(updateTableViews());
}

QHash<QString, AddOn*> StelAddOnMgr::loadAddonCatalog(QString jsonPath) const
{
	QHash<QString, AddOn*> addons;
	QFile jsonFile(jsonPath);
	if (!jsonFile.open(QIODevice::ReadOnly))
	{
		qWarning() << "[Add-on] Cannot open the catalog!"
			   << QDir::toNativeSeparators(jsonPath);
		return addons;
	}

	QJsonObject json(QJsonDocument::fromJson(jsonFile.readAll()).object());
	jsonFile.close();

	if (json["name"].toString() != "Add-ons Catalog" ||
		json["format"].toInt() != ADDON_CATALOG_VERSION)
	{
		qWarning()  << "[Add-on] The current catalog is not compatible!";
		return addons;
	}

	qDebug() << "[Add-on] loading catalog file:"
		 << QDir::toNativeSeparators(jsonPath);

	QVariantMap map = json["add-ons"].toObject().toVariantMap();
	QVariantMap::iterator i;
	for (i = map.begin(); i != map.end(); ++i)
	{
		AddOn* addon = new AddOn(i.key(), i.value().toMap());
		if (addon && addon->isValid())
		{
			addons.insert(addon->getAddOnId(), addon);
		}
	}

	return addons;
}

void StelAddOnMgr::restoreDefaultAddonJsonFile()
{
	QString path = StelFileMgr::getInstallationDir() % QDir::separator()
			% "data" % QDir::separator()
			% StelUtils::getApplicationSeries() % ".zip";
	qDebug() << "[Add-on] restoring default add-on catalog!" << path;
	installAddOnFromFile(path);
}

void StelAddOnMgr::setLastUpdate(const QDateTime& lastUpdate)
{
	m_lastUpdate = lastUpdate;
	m_pConfig->setValue(ADDON_CONFIG_PREFIX + "/last_update", m_lastUpdate.toString(Qt::ISODate));
}

void StelAddOnMgr::setUpdateFrequency(const UpdateFrequency& freq)
{
	m_eUpdateFrequency = freq;
	m_pConfig->setValue(ADDON_CONFIG_PREFIX + "/update_frequency", m_eUpdateFrequency);
}

void StelAddOnMgr::setUrl(const QString& url)
{
	m_sUrl = url;
	m_pConfig->setValue(ADDON_CONFIG_PREFIX + "/url", m_sUrl);
}

void StelAddOnMgr::loadConfig()
{
	setLastUpdate(m_pConfig->value(ADDON_CONFIG_PREFIX + "last_update", m_lastUpdate).toDateTime());
	setUpdateFrequency((UpdateFrequency) m_pConfig->value(ADDON_CONFIG_PREFIX + "update_frequency", m_eUpdateFrequency).toInt());
	setUrl(m_pConfig->value(ADDON_CONFIG_PREFIX + "/url", m_sUrl).toString());
}

void StelAddOnMgr::installAddons(QSet<AddOn *> addons)
{
	foreach (AddOn* addon, addons) {
		installAddOn(addon);
	}
}

void StelAddOnMgr::removeAddons(QSet<AddOn *> addons)
{
	foreach (AddOn* addon, addons) {
		removeAddOn(addon);
	}
}

void StelAddOnMgr::installAddOnFromFile(QString filePath)
{
	AddOn* addon = getAddOnFromZip(filePath);
	if (addon == NULL || !addon->isValid())
	{
		return;
	}
	else if (!filePath.startsWith(StelFileMgr::getAddonDir()))
	{
		if (!QFile(filePath).copy(addon->getZipPath()))
		{
			qWarning() << "[Add-on] Unable to copy"
				   << addon->getAddOnId() << "to"
				   << QDir::toNativeSeparators(addon->getZipPath());
			return;
		}
	}

	// checking if this addonId is in the catalog
	AddOn* addonInHash = m_addonsAvailable.value(addon->getAddOnId());
	addonInHash = addonInHash ? addonInHash : m_addonsToUpdate.value(addon->getAddOnId());

	if (addonInHash)
	{
		// addonId exists, but file is different!
		// do not install it! addonIds must be unique.
		if (addon->getChecksum() != addonInHash->getChecksum())
		{
			// TODO: asks the user if he wants to overwrite?
			qWarning() << "[Add-on] An addon ("
				   << addon->getTypeString()
				   << ") with the ID"
				   << addon->getAddOnId()
				   << "already exists. Aborting installation!";
			return;
		}
		else
		{
			// same file! just install it!
			installAddOn(addonInHash, false);
		}
	}
	else
	{
		installAddOn(addon, false);
		if (addon->getStatus() == AddOn::FullyInstalled)
		{
			insertAddonInJson(addon, m_sUserAddonJsonPath);
			m_addonsInstalled.insert(addon->getAddOnId(), addon);
		}
	}
}

void StelAddOnMgr::installAddOn(AddOn* addon, bool tryDownload)
{
	if (m_pDownloadMgr->isDownloading(addon))
	{
		return;
	}
	else if (!addon || !addon->isValid())
	{
		qWarning() << "[Add-on] Unable to install"
			   << QDir::toNativeSeparators(addon->getZipPath())
			   << "AddOn is not compatible!";
		return;
	}

	QFile file(addon->getZipPath());
	// checking if we have this file in the add-on dir (local disk)
	if (!file.exists())
	{
		addon->setStatus(AddOn::NotInstalled);
	}
	// only accept zip archive
	else if (!addon->getZipPath().endsWith(".zip"))
	{
		addon->setStatus(AddOn::InvalidFormat);
		qWarning() << "[Add-on] Error" << addon->getAddOnId()
			   << "The file found is not a .zip archive";
	}
	// checking integrity
	else if (addon->getType() != AddOn::ADDON_CATALOG && addon->getChecksum() != calculateMd5(file))
	{
		addon->setStatus(AddOn::Corrupted);
		qWarning() << "[Add-on] Error: File "
			   << QDir::toNativeSeparators(addon->getZipPath())
			   << " is corrupt, MD5 mismatch!";
	}
	else
	{
		// installing files
		addon->setStatus(AddOn::Installing);
		unzip(*addon);
		reloadCatalogues();
		refreshType(addon->getType());
		// remove zip archive from ~/.stellarium/addon/
		QFile(addon->getZipPath()).remove();
	}

	// require restart
	if ((addon->getStatus() == AddOn::PartiallyInstalled || addon->getStatus() == AddOn::FullyInstalled) &&
		(addon->getType() == AddOn::PLUGIN_CATALOG || addon->getType() == AddOn::STAR_CATALOG ||
		 addon->getType() == AddOn::LANG_SKYCULTURE || addon->getType() == AddOn::LANG_STELLARIUM ||
		 addon->getType() == AddOn::TEXTURE))
	{
		emit(restartRequired());
		addon->setStatus(AddOn::Restart);
	}
	// something goes wrong (file not found OR corrupt).
	// if applicable, try downloading it...
	else if (tryDownload && (addon->getStatus() == AddOn::NotInstalled || addon->getStatus() == AddOn::Corrupted))
	{
		addon->setStatus(AddOn::Installing);
		m_pDownloadMgr->download(addon);
	}
}

void StelAddOnMgr::removeAddOn(AddOn* addon)
{
	if (!addon || !addon->isValid())
	{
		return;
	}

	addon->setStatus(AddOn::NotInstalled);
	QStringList installedFiles = addon->getInstalledFiles();
	foreach (QString filePath, addon->getInstalledFiles()) {
		QFile file(filePath);
		if (!file.exists() || file.remove())
		{
			installedFiles.removeOne(filePath);
			QDir dir(filePath);
			dir.cdUp();
			QString dirName = dir.dirName();
			dir.cdUp();
			dir.rmdir(dirName); // try to remove dir
		}
		else
		{
			addon->setStatus(AddOn::PartiallyInstalled);
			qWarning() << "[Add-on] Unable to remove"
				   << QDir::toNativeSeparators(filePath);
		}
	}

	if (addon->getStatus() == AddOn::NotInstalled)
	{
		removeAddonFromJson(addon, m_sInstalledAddonsJsonPath);
		qDebug() << "[Add-on] Successfully removed: " << addon->getAddOnId();
	}
	else if (addon->getStatus() == AddOn::PartiallyInstalled)
	{
		qWarning() << "[Add-on] Partially removed: " << addon->getAddOnId();
	}
	else
	{
		addon->setStatus(AddOn::UnableToRemove);
		qWarning() << "[Add-on] Unable to remove: " << addon->getAddOnId();
		return; // nothing changed
	}

	addon->setInstalledFiles(installedFiles);

	if (addon->getType() == AddOn::PLUGIN_CATALOG || addon->getType() == AddOn::STAR_CATALOG ||
		addon->getType() == AddOn::LANG_SKYCULTURE || addon->getType() == AddOn::LANG_STELLARIUM ||
		addon->getType() == AddOn::TEXTURE)
	{
		emit (restartRequired());
		addon->setStatus(AddOn::Restart);
	}

	reloadCatalogues();
	refreshType(addon->getType());
}

AddOn* StelAddOnMgr::getAddOnFromZip(QString filePath)
{
	Stel::QZipReader reader(filePath);
	if (reader.status() != Stel::QZipReader::NoError)
	{
		qWarning() << "StelAddOnMgr: Unable to open the ZIP archive:"
			   << QDir::toNativeSeparators(filePath);
		return NULL;
	}

	foreach(Stel::QZipReader::FileInfo info, reader.fileInfoList())
	{
		if (!info.isFile || !info.filePath.endsWith("info.json"))
		{
			continue;
		}

		QByteArray data = reader.fileData(info.filePath);
		if (!data.isEmpty())
		{
			QJsonObject json(QJsonDocument::fromJson(data).object());
			qDebug() << "[Add-on] loading catalog file:"
				 << QDir::toNativeSeparators(info.filePath);

			QString addonId = json.keys().at(0);
			QVariantMap attributes = json.value(addonId).toObject().toVariantMap();
			QFile zipFile(filePath);
			QString md5sum = calculateMd5(zipFile);
			attributes.insert("checksum", md5sum);
			attributes.insert("download-size", zipFile.size() / 1024.0);
			attributes.insert("download-filename", QFileInfo(zipFile).fileName());

			return new AddOn(addonId, attributes);
		}
	}
	return NULL;
}

QList<AddOn*> StelAddOnMgr::scanFilesInAddOnDir()
{
	// check if there is any zip archives in the ~/.stellarium/addon dir
	QList<AddOn*> addons;
	QDir dir(StelFileMgr::getAddonDir());
	dir.setFilter(QDir::Files | QDir::Readable | QDir::NoDotAndDotDot);
	dir.setNameFilters(QStringList("*.zip"));
	foreach (QFileInfo fileInfo, dir.entryInfoList())
	{
		AddOn* addon = getAddOnFromZip(fileInfo.absoluteFilePath());
		if (addon)	// get all addons, even the incompatibles
		{
			addons.append(addon);
		}
	}
	return addons;
}

QString StelAddOnMgr::calculateMd5(QFile& file) const
{
	QString checksum;
	if (file.open(QIODevice::ReadOnly)) {
		QCryptographicHash md5(QCryptographicHash::Md5);
		md5.addData(file.readAll());
		checksum = md5.result().toHex();
		file.close();
	}
	return checksum;
}

void StelAddOnMgr::unzip(AddOn& addon)
{
	Stel::QZipReader reader(addon.getZipPath());
	if (reader.status() != Stel::QZipReader::NoError)
	{
		qWarning() << "[Add-on] Unable to open the ZIP archive:"
			   << QDir::toNativeSeparators(addon.getZipPath());
		addon.setStatus(AddOn::UnableToRead);
	}

	QStringList installedFiles = addon.getInstalledFiles();
	addon.setStatus(AddOn::FullyInstalled);
	foreach(Stel::QZipReader::FileInfo info, reader.fileInfoList())
	{
		if (!info.isFile || info.filePath.contains("info.json"))
		{
			continue;
		}

		QStringList validDirs;
		validDirs << "addon/" << "landscapes/" << "modules/" << "scripts/"
			  << "skycultures/" << "stars/" << "textures/" << "translations/";
		bool validDir = false;
		foreach (QString dir, validDirs)
		{
			if (info.filePath.startsWith(dir))
			{
				validDir = true;
				break;
			}
		}

		if (!validDir)
		{
			qWarning() << "[Add-on] Unable to install! Invalid destination"
				   << info.filePath;
			addon.setStatus(AddOn::InvalidDestination);
			return;
		}

		QFileInfo fileInfo(StelFileMgr::getUserDir() % "/" % info.filePath);
		StelFileMgr::makeSureDirExistsAndIsWritable(fileInfo.absolutePath());
		QFile file(fileInfo.absoluteFilePath());

		file.remove(); // overwrite
		QByteArray data = reader.fileData(info.filePath);
		if (!file.open(QIODevice::WriteOnly))
		{
			qWarning() << "[Add-on] cannot open file"
				   << QDir::toNativeSeparators(info.filePath);
			addon.setStatus(AddOn::UnableToWrite);
			continue;
		}
		file.write(data);
		file.close();
		installedFiles.append(fileInfo.absoluteFilePath());
		qDebug() << "[Add-on] New file installed:"
			 << QDir::toNativeSeparators(info.filePath);
	}
	installedFiles.removeDuplicates();
	addon.setInstalledFiles(installedFiles);
	insertAddonInJson(&addon, m_sInstalledAddonsJsonPath);
}

void StelAddOnMgr::insertAddonInJson(AddOn* addon, QString jsonPath)
{
	QFile jsonFile(jsonPath);
	if (jsonFile.open(QIODevice::ReadWrite))
	{
		QJsonObject attributes;
		attributes.insert("type", addon->getTypeString());
		attributes.insert("title", addon->getTitle());
		attributes.insert("description", addon->getDescription());
		attributes.insert("version", addon->getVersion());
		attributes.insert("date", addon->getDate().toString("yyyy.MM.dd"));
		attributes.insert("license", addon->getLicenseName());
		attributes.insert("license-url", addon->getLicenseURL());
		attributes.insert("download-url", addon->getDownloadURL());
		attributes.insert("download-filename", addon->getDownloadFilename());
		attributes.insert("download-size", addon->getDownloadSize());
		attributes.insert("checksum", addon->getChecksum());
		attributes.insert("textures", addon->getAllTextures().join(","));

		attributes.insert("status", addon->getStatus());
		attributes.insert("installed-files", QJsonArray::fromStringList(addon->getInstalledFiles()));

		QJsonArray authors;
		foreach (AddOn::Authors a, addon->getAuthors())
		{
			QJsonObject author;
			author.insert("name", a.name);
			author.insert("email", a.email);
			author.insert("url", a.url);
			authors.append(author);
		}
		attributes.insert("authors", authors);

		QJsonObject json(QJsonDocument::fromJson(jsonFile.readAll()).object());
		json.insert("name", QString("Add-ons Catalog"));
		json.insert("format", ADDON_CATALOG_VERSION);

		QJsonObject addons = json["add-ons"].toObject();
		addons.insert(addon->getAddOnId(), attributes);
		json.insert("add-ons", addons);

		jsonFile.resize(0);
		jsonFile.write(QJsonDocument(json).toJson());
		jsonFile.close();
	}
	else
	{
		qWarning() << "Add-On Mgr: Couldn't open the user catalog of addons!"
			   << QDir::toNativeSeparators(m_sInstalledAddonsJsonPath);
	}
}

void StelAddOnMgr::removeAddonFromJson(AddOn *addon, QString jsonPath)
{
	QFile jsonFile(jsonPath);
	if (jsonFile.open(QIODevice::ReadWrite))
	{
		QJsonObject json(QJsonDocument::fromJson(jsonFile.readAll()).object());
		QJsonObject addons = json.take("add-ons").toObject();
		addons.remove(addon->getAddOnId());
		json.insert("add-ons", addons);

		jsonFile.resize(0);
		jsonFile.write(QJsonDocument(json).toJson());
		jsonFile.close();
	}
	else
	{
		qWarning() << "[Add-on] Unable to open the catalog: "
			   << QDir::toNativeSeparators(jsonPath);
	}
}

void StelAddOnMgr::refreshType(AddOn::Type type)
{
	if (type == AddOn::LANDSCAPE)
	{
		emit (landscapesChanged());
	}
	else if (type == AddOn::SCRIPT)
	{
		emit (scriptsChanged());
	}
	else if (type == AddOn::SKY_CULTURE)
	{
		emit (skyCulturesChanged());
	}
}
