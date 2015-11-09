
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
#include "3rdparty/quazip/quazipfile.h"
#include <QThread>
#include <QFuture>
#include <QFutureWatcher>
#include <QtConcurrentRun>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include "gcsversioninfo.h"
#include "coreplugin/coreconstants.h"

AutoUpdater::AutoUpdater(QWidget *parent, UAVObjectManager * ObjMngr, int refreshIntervalSec, bool usePrereleases, QString gitHubUrl,  QString gitHubUsername, QString gitHubPassword) : usePrereleases(usePrereleases), mainAppApi(this), helperAppApi(this), dialog(NULL), parent(parent)
{
    refreshTimer.setInterval(refreshIntervalSec * 1000);
    mainAppApi.setRepo(gitHubUrl);
    if(!gitHubUsername.isEmpty() && !gitHubPassword.isEmpty())
        mainAppApi.setCredentials(gitHubUsername, gitHubPassword);
    helperAppApi.setRepo("PTDreamer", "copyApp");

    if(refreshIntervalSec != 0)
        refreshTimer.start();
    connect(this, SIGNAL(updateFound(gitHubReleaseAPI::release)), this, SLOT(onUpdateFound(gitHubReleaseAPI::release)));
    connect(this, SIGNAL(progressMessage(QString)), this, SLOT(onProgressText(QString)));
    connect(this, SIGNAL(decompressProgress(int)), this, SLOT(onProgress(int)));
    connect(this, SIGNAL(currentOperationMessage(QString)), this, SLOT(onNewOperation(QString)));

    connect(&refreshTimer, SIGNAL(timeout()), this, SLOT(onRefreshTimerTimeout()));
    connect(&mainAppApi, SIGNAL(logError(QString)), this, SLOT(sstr(QString)));
    connect(&mainAppApi, SIGNAL(logInfo(QString)), this, SLOT(sstr(QString)));
    connect(&mainAppApi, SIGNAL(downloadProgress(qint64,qint64)), this, SLOT(downloadProgress(qint64,qint64)));
    connect(&helperAppApi, SIGNAL(downloadProgress(qint64,qint64)), this, SLOT(downloadProgress(qint64,qint64)));
    process = new QProcess(parent);
    connect(process, SIGNAL(finished(int,QProcess::ExitStatus)), this, SLOT(processOutputAvailable()));
}

void AutoUpdater::refreshSettings(int refreshInterval, bool usePrereleases, QString gitHubUrl, QString gitHubUsername, QString gitHubPassword)
{
    refreshTimer.setInterval(refreshInterval *1000);
    usePrereleases = usePrereleases;
    mainAppApi.setRepo(gitHubUrl);
    if(!gitHubUsername.isEmpty() && !gitHubPassword.isEmpty())
        mainAppApi.setCredentials(gitHubUsername, gitHubPassword);
    if(refreshInterval == 0)
        refreshTimer.stop();
    else
        refreshTimer.start();
}

void AutoUpdater::onRefreshTimerTimeout()
{
    bool isUpdateFound = false;

    if(!usePrereleases) {
        mostRecentRelease = mainAppApi.getLatestRelease();
        if(mainAppApi.getLastError() != gitHubReleaseAPI::NO_ERROR)
            return;
    }
    else {
        QHash<int, gitHubReleaseAPI::release> releaseList = mainAppApi.getReleases();
        if(mainAppApi.getLastError() != gitHubReleaseAPI::NO_ERROR)
            return;
        mostRecentRelease = releaseList.values().first();
        foreach (gitHubReleaseAPI::release release, releaseList.values()) {
            qDebug() << release.published_at << mostRecentRelease.published_at;
            if(release.published_at > mostRecentRelease.published_at)
                mostRecentRelease = release;
        }
    }
    packageVersionInfo info;
    info = parsePackageVersionInfo(mostRecentRelease);
    if(!info.isValid)
        return;
    if(info.isNewer)
        isUpdateFound = true;
    if(true || isUpdateFound)//remove true
        emit updateFound(mostRecentRelease, info);
}

void AutoUpdater::onUpdateFound(gitHubReleaseAPI::release release, packageVersionInfo info)
{
    if(!dialog.isNull()) {
        return;
    }
    dialog = new updaterFormDialog(release.body, info.isUAVODifferent, parent);
    dialog.data()->show();
    dialog.data()->raise();
    dialog.data()->activateWindow();
    refreshTimer.stop();
    connect(dialog.data(), SIGNAL(startUpdate()), this, SLOT(onDialogStartUpdate()));
    connect(dialog.data(), SIGNAL(cancelDownload()), &mainAppApi, SLOT(abortOperation()));
    connect(dialog.data(), SIGNAL(dialogAboutToClose(bool)), this, SLOT(onCancel(bool)));
    connect(dialog.data(), SIGNAL(cancelDownload()), &helperAppApi, SLOT(abortOperation()));
}

void AutoUpdater::onDialogStartUpdate()
{
    QString preferredPlatformStr;
    QString compatiblePlatformStr;
#ifdef Q_OS_LINUX
    preferredPlatformStr = "linux";
#elif defined (Q_OS_WIN)
    preferredPlatformStr = "winx86";
#elif defined (Q_OS_MAC)
    preferredPlatformStr = "osx";
#endif
    if(QSysInfo::WordSize == 64) {
        compatiblePlatformStr = QString("%0_%1").arg(preferredPlatformStr).arg(32);
    }
    preferredPlatformStr.append(QString("_%0").arg(QSysInfo::WordSize));
    int copyAppID = -1;
    emit currentOperationMessage(tr("Looking for latest helper application"));
    QHash<int, gitHubReleaseAPI::GitHubAsset> copyAppAssets = helperAppApi.getLatestRelease().assets;
    if(copyAppAssets.values().size() == 0) {
        QMessageBox::critical(parent, tr("ERROR"), tr("Could not retrieve helper application needed for the update process"));
        dialog.data()->close();
        return;
    }
    foreach (int assetID, copyAppAssets.keys()) {
        if(copyAppAssets.value(assetID).label.contains(compatiblePlatformStr, Qt::CaseInsensitive)) {
            copyAppID = assetID;
        }
        if(copyAppAssets.value(assetID).label.contains(preferredPlatformStr, Qt::CaseInsensitive)) {
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
        qDebug() << mostRecentRelease.assets.value(assetID).label << mostRecentRelease.assets.value(assetID).name << compatiblePlatformStr << preferredPlatformStr;
        if(mostRecentRelease.assets.value(assetID).label.contains(compatiblePlatformStr, Qt::CaseInsensitive)) {
            appID = assetID;
        }
        if(mostRecentRelease.assets.value(assetID).label.contains(preferredPlatformStr, Qt::CaseInsensitive)) {
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
    copyAppFile.write(helperAppApi.downloadAsset(copyAppID));
    copyAppFile.close();

    emit currentOperationMessage(tr("Downloading latest application file package"));
    QByteArray fileData = mainAppApi.downloadAsset(appID);
    QFile file;
    file.setFileName(QDir::tempPath() + QDir::separator() + mostRecentRelease.assets.value(appID).name);
    if(!file.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(parent, tr("ERROR"), QString(tr("Could not open file for writing %0")).arg(file.fileName()));
        return;
    }
    file.write(fileData);
    emit currentOperationMessage(tr("Decompressing new application compressed file"));
#ifdef Q_OS_WIN
#define EXEC "taulabsgcs.exe"
#define COPY_APP_EXEC "copyApp.exe"
#else
#define EXEC "taulabsgcs"
#define COPY_APP_EXEC "copyApp"
#endif

    AutoUpdater::decompressResult appDecompressResult = fileDecompress(file.fileName(), QDir::tempPath(), QFileInfo(qApp->applicationFilePath()).fileName());
    if(!(appDecompressResult.success && appDecompressResult.execPath.exists())) {
        QMessageBox::critical(parent, tr("ERROR"), tr("Something went wrong during file decompression!"));
        return;
    }
    emit currentOperationMessage(tr("Decompressing helper application"));
    AutoUpdater::decompressResult copyAppDecompressResult = fileDecompress(copyAppFile.fileName(), QDir::tempPath(), COPY_APP_EXEC);
    qDebug() << copyAppDecompressResult.execPath << appDecompressResult.execPath;
    if(!(copyAppDecompressResult.success && copyAppDecompressResult.execPath.exists())) {
        QMessageBox::critical(parent, tr("ERROR"), tr("Something went wrong during file decompression!"));
        return;
    }
    QDir appDir = QApplication::applicationDirPath();
    appDir.cdUp();
    QMessageBox::information(parent, tr("Update Ready"), tr("The file fetching process is completed, press ok to continue the update"));
    appDecompressResult.execPath.cdUp();
    process->startDetached(copyAppDecompressResult.execPath.absolutePath() + QDir::separator() + COPY_APP_EXEC, QStringList() << appDecompressResult.execPath.absolutePath() <<  appDir.absolutePath() << QApplication::applicationFilePath());
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
    if(!dialog.isNull())
        dialog.data()->setOperation(newOp);
}

void AutoUpdater::onProgressText(QString newTxt)
{
    if(!dialog.isNull())
        dialog.data()->setProgress(newTxt);
}

void AutoUpdater::onProgress(int value)
{
    if(!dialog.isNull())
        dialog.data()->setProgress(value);
}

#ifdef Q_OS_LINUX
AutoUpdater::decompressResult AutoUpdater::fileDecompress(QString fileName, QString destinationPath, QString execFile)
{
    AutoUpdater::decompressResult ret;
    qDebug() << "1-" << fileName << destinationPath;
    QFile file(fileName);
    if(!file.exists()) {
        ret.success = false;
        return ret;
    }
    QString cmd = QString("/bin/sh -c \"xz -l %0 | grep -oh -m2 \"[0-9]*\\.[0-9].MiB\" | tail -1\"").arg(fileName);
    QEventLoop loop;
    QProcess process;
    connect(&process, SIGNAL(finished(int)), &loop, SLOT(quit()));
    process.start(cmd);
    loop.exec();
    if(process.exitStatus() != QProcess::NormalExit) {
        ret.success = false;
        return ret;
    }
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
                if(QString(receiveBuffer.split(QRegExp("\\s+")).value(5)).split(QDir::separator()).last() == execFile) {
                    ret.execPath = QDir(QFileInfo(receiveBuffer.split(QRegExp("\\s+")).value(5)).absolutePath());
                    ret.execPath.cdUp();
                }
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
    if(process.exitStatus() != QProcess::NormalExit) {
        ret.success = false;
        return ret;
    }
    emit decompressProgress(100);
    ret.success = true;
    return ret;
}
#endif
#ifdef Q_OS_WIN
AutoUpdater::decompressResult AutoUpdater::fileDecompress(QString zipfile, QString destinationPath, QString execFile)
{
    AutoUpdater::decompressResult res;
    QEventLoop loop;
    QFutureWatcher<AutoUpdater::decompressResult> watcher;
    connect(&watcher, SIGNAL(finished()), &loop, SLOT(quit()));
    if(!QFile(zipfile).exists())
        return res;
    QFuture <AutoUpdater::decompressResult> future = QtConcurrent::run(this, &AutoUpdater::winFileDecompress, zipfile, destinationPath, execFile);
    watcher.setFuture(future);
    loop.exec();
    return future.result();
}

AutoUpdater::decompressResult AutoUpdater::winFileDecompress(QString zipfile, QString destinationPath, QString exeFile)
{
    AutoUpdater::decompressResult ret;
    qDebug() << "winFileDecompress" << zipfile << destinationPath;
    qint64 totalDecompressedSize = 0;
    QDir destination(destinationPath);
    QuaZip archive(zipfile);
    if(!archive.open(QuaZip::mdUnzip)) {
        ret.success = false;
        return ret;
    }
    qDebug() << "winFileDecompress open";
    for( bool f = archive.goToFirstFile(); f; f = archive.goToNextFile() )
    {
        QuaZipFileInfo *info = new QuaZipFileInfo;
        if(archive.getCurrentFileInfo(info)) {
            totalDecompressedSize += info->uncompressedSize;
            qDebug() << "winFileDecompress" << archive.getCurrentFileName() << info->uncompressedSize << totalDecompressedSize;
        }
    }
    qint64 currentDecompressedSize = 0;
    for( bool f = archive.goToFirstFile(); f; f = archive.goToNextFile() )
    {
        // set source file in archive
        QString filePath = archive.getCurrentFileName();
        qDebug() << "decompress" << filePath;
        QuaZipFile zFile( archive.getZipName(), filePath );
        // open the source file
        if(!zFile.open( QIODevice::ReadOnly )) {
            ret.success = false;
            return ret;
        }
        // create a bytes array and write the file data into it
        QByteArray ba = zFile.readAll();
        currentDecompressedSize += ba.size();
        emit decompressProgress(currentDecompressedSize * 100 / totalDecompressedSize);
        qDebug() << "decompress " << currentDecompressedSize * 100 / totalDecompressedSize;
        // close the source file
        zFile.close();
        // set destination file
        QFileInfo file(filePath);
        if(filePath.endsWith("/")) {
            QDir().mkdir(destination.absolutePath() + QDir::separator() + filePath);
            qDebug() << "decompress" << "ISDIR";
        }
        else {
            emit progressMessage(QString("Extracting %0").arg(file.fileName()));
            qDebug() << "decompress FILE" << destination.absolutePath() << file.fileName();
            QDir dir = QFileInfo(destination.absolutePath()+ QDir::separator() + filePath).absolutePath();
            if(!exeFile.isEmpty() && (exeFile == file.fileName()))
                ret.execPath = dir;
            if(!dir.exists())
                QDir().mkdir(dir.absolutePath());
            QFile dstFile(destination.absolutePath()+ QDir::separator() + filePath);
            qDebug() << destination.absolutePath()+ QDir::separator() + filePath;
            // open the destination file
            if(!dstFile.open( QIODevice::WriteOnly)) {
                qDebug() << "decompress COULD NOT OPEN FILE FOR WRITE" << dstFile.fileName();
                ret.success = false;
                return ret;
            }
            qDebug() << "EXIT";
            // write the data from the bytes array into the destination file
            dstFile.write(ba);
            //close the destination file
            dstFile.close();
        }
    }
    emit decompressProgress(100);
    ret.success = true;
    return ret;
}
#endif

AutoUpdater::packageVersionInfo AutoUpdater::parsePackageVersionInfo(gitHubReleaseAPI::release release)
{
    AutoUpdater::packageVersionInfo ret;
    int versionInfoID = -1;
    foreach (int key, release.assets.keys()) {
        if(release.assets.value(key).name == "packageversioninfo.json") {
            versionInfoID = key;
        }
    }
    if(versionInfoID == -1) {
        ret.isValid = false;
        return ret;
    }
    QByteArray versionInfoData = mainAppApi.downloadAsset(versionInfoID);
    QJsonDocument doc = QJsonDocument::fromJson(versionInfoData);
    if(doc.isNull()) {
        ret.isValid = false;
        return ret;
    }
    QJsonObject obj = doc.object();
    QJsonValue val = obj.value("package_info");
    if(val.isUndefined()) {
        ret.isValid = false;
        return ret;
    }
    if(!val.toObject().value("uavo_hash").isUndefined())
        ret.uavo_hash = val.toObject().value("uavo_hash").toString();
    if(!val.toObject().value("uavo_hash_text").isUndefined())
        ret.uavo_hash_txt = val.toObject().value("uavo_hash_text").toString();
    if(!val.toObject().value("date").isUndefined())
        ret.date = QDateTime::fromString(val.toObject().value("date").toString(),"yyyyMMdd hh:mm");
    QDateTime currentGCSDate;
    QString versionData = QLatin1String(Core::Constants::GCS_REVISION_STR);
    currentGCSDate = QDateTime::fromString(versionData.split(" ").last(),"yyyyMMdd");
    if(ret.date > currentGCSDate)
        ret.isNewer = true;
    else
        ret.isNewer = false;
    QString uavoHash = QString::fromLatin1(Core::Constants::UAVOSHA1_STR);
    ret.isUAVODifferent = (uavoHash != ret.uavo_hash);
    return ret;
}
