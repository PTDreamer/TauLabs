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
#include <QApplication>
#include <QtMath>
#include <QSysInfo>

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
    qDebug() << "timeout";
   // emit updateFound(mostRecentRelease);//REMOVE
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
            qDebug() << release.published_at << mostRecentRelease.published_at;
            if(release.published_at > mostRecentRelease.published_at)
                mostRecentRelease = release;
        }
    }
    qDebug() << mostRecentRelease.name << mostRecentRelease.published_at;
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
    connect(this, SIGNAL(progressMessage(QString)), this, SLOT(onProgressText(QString)));
    connect(this, SIGNAL(decompressProgress(int)), this, SLOT(onProgress(int)));
    connect(this, SIGNAL(currentOperationMessage(QString)), this, SLOT(onNewOperation(QString)));
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
    osString.append(QString::number(QSysInfo::WordSize));
    gitHubReleaseAPI capi(parent);
    connect(&capi, SIGNAL(downloadProgress(qint64,qint64)), this, SLOT(downloadProgress(qint64,qint64)));
    connect(dialog.data(), SIGNAL(cancelDownload()), &capi, SLOT(abortOperation()));
    capi.setRepo("PTDreamer", "copyApp");

    int copyAppID = -1;
    emit currentOperationMessage(tr("Looking for latest helper application"));
    QHash<int, gitHubReleaseAPI::GitHubAsset> copyAppAssets = capi.getLatestRelease().assets;
    if(copyAppAssets.values().length() == 0) {
        QMessageBox::critical(parent, tr("ERROR"), tr("Could not retrieve helper application needed for the update process"));
        dialog.data()->close();
        return;
    }
    foreach (int assetID, copyAppAssets.keys()) {
        if(copyAppAssets.value(assetID).label.contains(osString, Qt::CaseInsensitive)) {
            copyAppID = assetID;
            break;
        }
    }
    if(copyAppID == -1) {
        QMessageBox::critical(parent, tr("ERROR"), tr("Could not retrieve helper application needed for the update process"));
        dialog.data()->close();
        return;
    }
    int appID = -1;
    emit currentOperationMessage(tr("Looking for latest application file package"));
    foreach (int assetID, mostRecentRelease.assets.keys()) {
        qDebug() << mostRecentRelease.assets.value(assetID).name;
        if(mostRecentRelease.assets.value(assetID).label.contains(osString, Qt::CaseInsensitive)) {
            appID = assetID;
            break;
        }
    }
    if(appID == -1) {
        QMessageBox::critical(parent, tr("ERROR"), tr("Could not retrieve application package needed for the update process"));
        dialog.data()->close();
        return;
    }
    QFile copyAppFile;
    copyAppFile.setFileName(QDir::tempPath() + QDir::separator() + copyAppAssets.value(copyAppID).name);
    copyAppFile.open(QIODevice::WriteOnly);
    emit currentOperationMessage(tr("Downloading latest helper application"));
    copyAppFile.write(capi.downloadAsset(copyAppID));
    copyAppFile.close();

    emit currentOperationMessage(tr("Downloading latest application file package"));
    QByteArray fileData = api.downloadAsset(appID);
    QFile file;
    file.setFileName(QDir::tempPath() + QDir::separator() + mostRecentRelease.assets.value(appID).name);
    if(!file.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(parent, tr("ERROR"), QString(tr("Could not open file for writing %0")).arg(file.fileName()));
        return;
    }
    file.write(fileData);
    emit currentOperationMessage(tr("Decompressing new application compressed file"));
    if(!fileDecompress(file.fileName(), QDir::tempPath())) {
        QMessageBox::critical(parent, tr("ERROR"), tr("Something went wrong during file decompression!"));
        return;
    }
    emit currentOperationMessage(tr("Decompressing helper application"));
    if(!fileDecompress(copyAppFile.fileName(), QDir::tempPath())) {
        QMessageBox::critical(parent, tr("ERROR"), tr("Something went wrong during file decompression!"));
        return;
    }
    QDir appDir = QApplication::applicationDirPath();
    appDir.cdUp();
    QMessageBox::information(parent, tr("Update Ready"), tr("The file fetching process is completed, press ok to continue the update"));
    process->startDetached(QDir::tempPath() + QDir::separator() + "package" + QDir::separator() + "copyApp", QStringList() << QDir::tempPath() + QDir::separator() + QFileInfo(file).baseName() <<  appDir.absolutePath() << QApplication::applicationFilePath());
    QApplication::quit();
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
        dialog.data()->setProgress(QString(tr("%0 of %1 bytes downloaded")).arg(progress).arg(total));
        int p = (progress * 100) / total;
        dialog.data()->setProgress(p);
    }
}

void AutoUpdater::onNewOperation(QString newOp)
{
    dialog.data()->setOperation(newOp);
}

void AutoUpdater::onProgressText(QString newTxt)
{
    dialog.data()->setProgress(newTxt);
}

void AutoUpdater::onProgress(int value)
{
    dialog.data()->setProgress(value);
}

bool AutoUpdater::fileDecompress(QString fileName, QString destinationPath)
{
    qDebug() << "1-" << fileName << destinationPath;
    QFile file(fileName);
    if(!file.exists())
        return false;
    QString cmd = QString("/bin/sh -c \"xz -l %0 | grep -oh -m2 \"[0-9]*\\.[0-9].MiB\" | tail -1\"").arg(fileName);
    QEventLoop loop;
    QProcess process;
    connect(&process, SIGNAL(finished(int)), &loop, SLOT(quit()));
    process.start(cmd);
    loop.exec();
    if(process.exitStatus() != QProcess::NormalExit)
        return false;
    QString totalSizeStr = process.readAll();
    QString unit = totalSizeStr.split(" ").value(1);
    QString value = totalSizeStr.split(" ").value(0);
    qint64 multiplier;
    if(unit.contains("KiB"))
        multiplier = qPow(2,10);
    else if(unit.contains("MiB"))
        multiplier = qPow(2,20);
    else if(unit.contains("GiB"))
        multiplier = qPow(2,30);
    else
        multiplier = 1;
    bool ok;
    qint64 size = value.replace(",", ".").toDouble(&ok) * multiplier;
    qint64 partial = 0;
    if(!ok)
        size = 0;
    cmd = QString("tar -xvvf %0 -C %1").arg(fileName).arg(destinationPath);
    process.start("/bin/sh", QStringList() << "-c" << cmd);
    connect(&process, SIGNAL(readyRead()), &loop, SLOT(quit()));
    QString receiveBuffer;
    qDebug() << "out WHILE";
    while(process.state() != QProcess::NotRunning) {
        qDebug() << "in WHILE";
        loop.exec();
        do {
            receiveBuffer = QString(process.readLine());
            if(!receiveBuffer.isEmpty()) {
                emit progressMessage(QString("Extracting %0").arg(QString(receiveBuffer.split(QRegExp("\\s+")).value(5)).split(QDir::separator()).last()));
                qDebug() << QString("Extracting %0").arg(receiveBuffer.split(QRegExp("\\s+")).value(5));
                if(size != 0) {
                    QString s = receiveBuffer.split(QRegExp("\\s+")).value(2);
                    if(true) {
                        partial += s.toInt();
                        emit decompressProgress(partial * 100 / size);
                    }
                }
            }
        } while (process.canReadLine());

    }
    if(process.exitStatus() != QProcess::NormalExit)
        return false;
    emit decompressProgress(100);
    return true;
}
