/**
 ******************************************************************************
 * @file       updaterformdialog.h
 * @author     Tau Labs, http://taulabs.org, Copyright (C) 2014
 * @addtogroup [Group]
 * @{
 * @addtogroup updaterFormDialog
 * @{
 * @brief [Brief]
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

#ifndef UPDATERFORMDIALOG_H
#define UPDATERFORMDIALOG_H

#include <QWidget>
#include <QDialog>

namespace Ui {
class updaterFormDialog;
}

class updaterFormDialog : public QDialog
{
    Q_OBJECT

public:
    explicit updaterFormDialog(QString releaseDetails, bool changedUAVO, QWidget *parent = 0);
    ~updaterFormDialog();

private:
    Ui::updaterFormDialog *ui;

public slots:
    void onStartUpdate();
    void onHide();
    void setProgress(QString text);
    void setProgress(int value);
    void setOperation(QString text);

private slots:
    void onCancelDownload();
    void onCancel();

signals:
    void cancelDownload();
    void cancel(bool dontShowAgain);
    void dialogAboutToClose(bool dontShowAgain);
    void startUpdate();
    void hide();

protected:
    void closeEvent(QCloseEvent *event);
};

#endif // UPDATERFORMDIALOG_H
