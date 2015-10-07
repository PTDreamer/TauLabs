/**
 ******************************************************************************
 * @file       autoupdater.h
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
#ifndef AUTOUPDATER_H
#define AUTOUPDATER_H
#include <QObject>
#include <QTimer>
#include "uavobjectmanager.h"
#include "githubreleaseapi/githubreleaseapi.h"
#include "autoupdater_global.h"
#include "updaterformdialog.h"
#include <QPointer>
#include <QProcess>

class AutoUpdater: public QObject
{
    Q_OBJECT
public:
    AutoUpdater(QWidget *parent, UAVObjectManager * ObjMngr, int refreshInterval, bool usePrereleases, QString gitHubUrl);
public slots:
    void refreshSettings(int refreshInterval, bool usePrereleases, QString gitHubUrl);
private slots:
    void onRefreshTimerTimeout();
    void onUpdateFound(gitHubReleaseAPI::release release);
    void onDialogUpdate();
    void onCancel(bool dontShowAgain);
    void sstr(QString str);
    void processOutputAvailable();
    void downloadProgress(qint64 progress, qint64 total);
    void onNewOperation(QString newOp);
    void onProgressText(QString newTxt);
    void onProgress(int value);
private:
    bool usePrereleases;
    QTimer refreshTimer;
    gitHubReleaseAPI api;
    QPointer<updaterFormDialog> dialog;
    gitHubReleaseAPI::release mostRecentRelease;
    QWidget *parent;
    QProcess *process;
    bool fileDecompress(QString fileName, QString destinationPath);
signals:
    void updateFound(gitHubReleaseAPI::release release);
    void decompressProgress(int progress);
    void progressMessage(QString);
    void currentOperationMessage(QString);
};

#endif // AUTOUPDATER_H
