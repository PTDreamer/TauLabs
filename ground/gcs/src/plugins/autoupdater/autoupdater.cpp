/**
 ******************************************************************************
 * @file       autoupdater.c
 * @author     Tau Labs, http://taulabs.org, Copyright (C) 2012-2015
 * @addtogroup GCSPlugins GCS Plugins
 * @{
 * @addtogroup AutoUpdater plugin
 * @{
 *
 * @brief Auto updates the GCS from GitHub releases
 *****************************************************************************/
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include "autoupdater.h"
#include <QtGlobal>
#include <QDir>
#include <QMessageBox>

AutoUpdater::AutoUpdater(QWidget *parent, UAVObjectManager * ObjMngr, int refreshInterval, bool usePrereleases, QString gitHubUrl) : usePrereleases(usePrereleases), api(this), dialog(NULL), parent(parent)
{
    refreshTimer.setInterval(refreshInterval * 1000);
    api.setRepo(gitHubUrl);
    if(refreshInterval != 0)
        refreshTimer.start();
    connect(this, SIGNAL(updateFound(gitHubReleaseAPI::release)), this, SLOT(onUpdateFound(gitHubReleaseAPI::release)));
    connect(&refreshTimer, SIGNAL(timeout()), this, SLOT(onRefreshTimerTimeout()));
    connect(&api, SIGNAL(logError(QString)), this, SLOT(sstr(QString)));
    connect(&api, SIGNAL(logInfo(QString)), this, SLOT(sstr(QString)));
    process = new QProcess(parent);
    connect(process, SIGNAL(finished(int,QProcess::ExitStatus)), this, SLOT(processOutputAvailable()));
}

void AutoUpdater::refreshSettings(int refreshInterval, bool usePrereleases, QString gitHubUrl)
{
    refreshTimer.setInterval(refreshInterval *1000);
    usePrereleases = usePrereleases;
    api.setRepo(gitHubUrl);
    if(refreshInterval == 0)
        refreshTimer.stop();
    else
        refreshTimer.start();
}

void AutoUpdater::onRefreshTimerTimeout()
{
    bool isUpdateFound = false;
    QDateTime currentGCS;
    if(!usePrereleases) {
        mostRecentRelease = api.getLatestRelease();
        if(api.getLastError() != gitHubReleaseAPI::NO_ERROR)
            return;
    }
    else {
        QHash<int, gitHubReleaseAPI::release> releaseList = api.getReleases();
        if(api.getLastError() != gitHubReleaseAPI::NO_ERROR)
            return;
        mostRecentRelease = releaseList.values().first();
        foreach (gitHubReleaseAPI::release release, releaseList.values()) {
            if(release.published_at > mostRecentRelease.published_at)
                mostRecentRelease = release;
        }
    }
    if(mostRecentRelease.published_at > currentGCS)
        isUpdateFound = true;
    if(true || isUpdateFound)
        emit updateFound(mostRecentRelease);
}

void AutoUpdater::onUpdateFound(gitHubReleaseAPI::release release)
{
    if(!dialog.isNull()) {
        qDebug() << "ALREADY OPEN";
        return;
    }
    dialog = new updaterFormDialog(release.body, parent);
    dialog.data()->show();
    dialog.data()->raise();
    dialog.data()->activateWindow();
    refreshTimer.stop();
    connect(dialog.data(), SIGNAL(startUpdate()), this, SLOT(onDialogUpdate()));
    connect(dialog.data(), SIGNAL(cancelDownload()), &api, SLOT(abortOperation()));
    connect(dialog.data(), SIGNAL(dialogAboutToClose(bool)), this, SLOT(onCancel(bool)));
    connect(&api, SIGNAL(downloadProgress(qint64,qint64)), this, SLOT(downloadProgress(qint64,qint64)));

}

void AutoUpdater::onDialogUpdate()
{
    QString osString;
#ifdef Q_OS_LINUX
    osString = "linux";
#elif defined (Q_OS_WIN)
    osString = "win";
#elif defined (Q_OS_MAC)
    osString = "dmg";
#endif
    int id;
    foreach (int assetID, mostRecentRelease.assets.keys()) {
        if(mostRecentRelease.assets.value(assetID).name.contains(osString, Qt::CaseInsensitive)) {
            id = assetID;
            break;
        }
    }
    QByteArray fileData = api.downloadAsset(id);
    QFile file;
    file.setFileName(QDir::tempPath() + QDir::separator() + mostRecentRelease.assets.value(id).name);
    if(!file.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(parent, tr("ERROR"), tr("Could not open file for writing"));
        return;
    }
    file.write(fileData);
    QEventLoop loop;
    connect(process, SIGNAL(finished(int)), &loop, SLOT(quit()));
    process->start("tar", QStringList() << "-tf" << file.fileName());
    loop.exec();
    totalProgressLines = 0;
    currentProgressLines = 0;
    if(process->exitCode() == 0) {
        QString str(process->readAll());
        QStringList lineList = str.split("\n");
        totalProgressLines = lineList.count();
    }
    connect(process, SIGNAL(readyRead()), this, SLOT(decompressProgress()));
    process->start("tar", QStringList() << "-xvf" << file.fileName() << "-C" << QDir::tempPath());
}

void AutoUpdater::onCancel(bool dontShowAgain)
{
    if(dontShowAgain)
        refreshTimer.stop();
    else
        refreshTimer.start();
}

void AutoUpdater::sstr(QString str)
{
    qDebug() << str;
}

void AutoUpdater::processOutputAvailable()
{

}

void AutoUpdater::downloadProgress(qint64 progress, qint64 total)
{
    if(total <= 0)
        return;
    if(!dialog.isNull()) {
        dialog.data()->setOperation(tr("Downloading"));
        dialog.data()->setProgress(QString(tr("%0 of %1 bytes downloaded")).arg(progress).arg(total));
        int p = (progress * 100) / total;
        dialog.data()->setProgress(p);
    }
}

void AutoUpdater::decompressProgress()
{
    QString str(process->readAll());
    QStringList lineList = str.split("\n");
    currentProgressLines += lineList.count();
    int p = (currentProgressLines * 100) / totalProgressLines;
    if(!dialog.isNull()) {
        dialog.data()->setOperation(tr("Decompressing"));
        dialog.data()->setProgress(p);
        QString str = lineList.first();
        str = str.split(QDir::separator()).last();
        dialog.data()->setProgress(QString(tr("Decompressing %0")).arg(str));
    }
}
