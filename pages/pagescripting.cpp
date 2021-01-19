/*
    Copyright 2021 Benjamin Vedder	benjamin@vedder.se

    This file is part of VESC Tool.

    VESC Tool is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    VESC Tool is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
    */

#include "pagescripting.h"
#include "ui_pagescripting.h"
#include "widgets/helpdialog.h"

#include <QQmlEngine>
#include <QQmlContext>
#include <QSettings>
#include <QmlHighlighter>
#include <QDateTime>
#include <QFileDialog>
#include <QMessageBox>
#include <QDirIterator>
#include <QVescCompleter>

PageScripting::PageScripting(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::PageScripting)
{
    ui->setupUi(this);
    mVesc = nullptr;
    ui->qmlWidget->setResizeMode(QQuickWidget::SizeRootObjectToView);
    ui->searchWidget->setVisible(false);

    ui->qmlEdit->setHighlighter(new QmlHighlighter);
    ui->qmlEdit->setCompleter(new QVescCompleter);
    ui->qmlEdit->setTabReplaceSize(4);

    connect(ui->qmlEdit, &QCodeEditor::saveTriggered, [this]() {
        on_saveButton_clicked();
    });
    connect(ui->qmlEdit, &QCodeEditor::runEmbeddedTriggered, [this]() {
        on_runButton_clicked();
    });
    connect(ui->qmlEdit, &QCodeEditor::runWindowTriggered, [this]() {
        on_runWindowButton_clicked();
    });
    connect(ui->qmlEdit, &QCodeEditor::stopTriggered, [this]() {
        on_stopButton_clicked();
    });
    connect(ui->qmlEdit, &QCodeEditor::clearConsoleTriggered, [this]() {
        ui->debugEdit->clear();
    });
    connect(ui->qmlEdit, &QCodeEditor::searchTriggered, [this]() {
        ui->searchWidget->setVisible(true);
        auto selected = ui->qmlEdit->textCursor().selectedText();
        if (!selected.isEmpty()) {
            ui->searchEdit->setText(selected);
        } else {
            ui->qmlEdit->searchForString(ui->searchEdit->text());
        }
        ui->searchEdit->setFocus();
    });

    QSettings set;
    ui->qmlEdit->setPlainText(set.value("pagescripting/lastqml", "").toString());
    ui->fileNowLabel->setText(set.value("pagescripting/lastfilepath", "").toString());
    {
        int size = set.beginReadArray("pagescripting/recentfiles");
        for (int i = 0; i < size; ++i) {
            set.setArrayIndex(i);
            mRecentFiles.append(set.value("path").toString());
        }
        set.endArray();
    }

    updateRecentList();

    // Load examples
    QDirIterator it("://res/qml/Examples/");
    while (it.hasNext()) {
        QFileInfo fi(it.next());
        QListWidgetItem *item = new QListWidgetItem;
        item->setText(it.fileName());
        item->setData(Qt::UserRole, it.filePath());
        ui->exampleList->addItem(item);
    }
}

PageScripting::~PageScripting()
{
    QSettings set;
    set.setValue("pagescripting/lastqml", ui->qmlEdit->toPlainText());
    set.setValue("pagescripting/lastfilepath", ui->fileNowLabel->text());
    {
        set.remove("pagescripting/recentfiles");
        set.beginWriteArray("pagescripting/recentfiles");
        int ind = 0;
        for (auto f: mRecentFiles) {
            set.setArrayIndex(ind);
            set.setValue("path", f);
            ind++;
        }
        set.endArray();
    }

    delete ui;
}

VescInterface *PageScripting::vesc() const
{
    return mVesc;
}

void PageScripting::setVesc(VescInterface *vesc)
{
    mVesc = vesc;

    ui->qmlWidget->engine()->rootContext()->setContextProperty("VescIf", mVesc);
    ui->qmlWidget->engine()->rootContext()->setContextProperty("QmlUi", this);
}

void PageScripting::reloadParams()
{

}

void PageScripting::debugMsgRx(QtMsgType type, const QString msg)
{
    QString str;

    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
        str = "<font color=\"red\">" + msg + "</font><br>";
    } else {
        str = msg + "<br>";
    }

    ui->debugEdit->moveCursor(QTextCursor::End);
    ui->debugEdit->insertHtml("<font color=\"blue\">" +
                              QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss: ") +
                              "</font>" + str);
    ui->debugEdit->moveCursor(QTextCursor::End);
}

void PageScripting::on_runButton_clicked()
{
    ui->qmlWidget->setSource(QUrl(QLatin1String("qrc:/res/qml/DynamicLoader.qml")));
    emit reloadQml(ui->qmlEdit->toPlainText());
}

void PageScripting::on_stopButton_clicked()
{
    ui->qmlWidget->setSource(QUrl(QLatin1String("")));
    mQmlUi.stopCustomGui();
}

void PageScripting::on_runWindowButton_clicked()
{
    ui->runWindowButton->setEnabled(false);

    if (!mQmlUi.isCustomGuiRunning()) {
        mQmlUi.startCustomGui(mVesc);
    }

    QTimer::singleShot(10, [this]() {
        mQmlUi.emitReloadCustomGui("qrc:/res/qml/DynamicLoader.qml");
    });

    QTimer::singleShot(1000, [this]() {
        mQmlUi.emitReloadQml(ui->qmlEdit->toPlainText());
        ui->runWindowButton->setEnabled(true);
    });
}

void PageScripting::on_fullscreenButton_clicked()
{
    mQmlUi.emitToggleFullscreen();
}

void PageScripting::on_openFileButton_clicked()
{
    QString fileName = QFileDialog::getOpenFileName(this,
                                                    tr("Open QML File"), "",
                                                    tr("QML files (*.qml)"));

    if (!fileName.isEmpty()) {
        QFile file(fileName);
        if (!file.open(QIODevice::ReadOnly)) {
            QMessageBox::critical(this, "Open QML File",
                                  "Could not open\n" + fileName + "\nfor reading");
            return;
        }

        ui->qmlEdit->setPlainText(file.readAll());
        ui->fileNowLabel->setText(fileName);
        if (!mRecentFiles.contains(fileName)) {
            mRecentFiles.append(fileName);
            updateRecentList();
        }

        file.close();
    }
}

void PageScripting::on_saveButton_clicked()
{
    QString fileName = ui->fileNowLabel->text();

    QFileInfo fi(fileName);
    if (!fi.exists()) {
        QMessageBox::critical(this, "Save File",
                              "Current file not valid. Use save as instead.");
        return;
    }

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this, "Save QML File",
                              "Could not open\n" + fileName + "\nfor writing");
        return;
    }

    file.write(ui->qmlEdit->toPlainText().toUtf8());
    file.close();

    mVesc->emitStatusMessage("Saved " + fileName, true);
}

void PageScripting::on_saveAsButton_clicked()
{
    QString fileName = QFileDialog::getSaveFileName(this,
                                                    tr("Save QML"), "",
                                                    tr("QML Files (*.qml)"));

    if (!fileName.isEmpty()) {
        if (!fileName.toLower().endsWith(".qml")) {
            fileName.append(".qml");
        }

        QFile file(fileName);
        if (!file.open(QIODevice::WriteOnly)) {
            QMessageBox::critical(this, "Save QML File",
                                  "Could not open\n" + fileName + "\nfor writing");
            return;
        }

        file.write(ui->qmlEdit->toPlainText().toUtf8());
        file.close();

        ui->fileNowLabel->setText(fileName);
        if (!mRecentFiles.contains(fileName)) {
            mRecentFiles.append(fileName);
            updateRecentList();
        }

        mVesc->emitStatusMessage("Saved " + fileName, true);
    }
}

void PageScripting::on_openRecentButton_clicked()
{
    auto *item = ui->recentList->currentItem();

    if (item) {
        QString fileName = item->text();
        QFile file(fileName);

        if (!file.open(QIODevice::ReadOnly)) {
            QMessageBox::critical(this, "Open QML File",
                                  "Could not open\n " + fileName + " for reading");
            return;
        }

        ui->qmlEdit->setPlainText(file.readAll());
        ui->fileNowLabel->setText(fileName);

        file.close();
    } else {
        QMessageBox::critical(this, "Open Recent",
                              "Please select a file.");
    }
}

void PageScripting::on_removeSelectedButton_clicked()
{
    auto *item = ui->recentList->currentItem();

    if (item) {
        QString fileName = item->text();
        mRecentFiles.removeOne(fileName);
        updateRecentList();
    }
}

void PageScripting::on_clearRecentButton_clicked()
{
    mRecentFiles.clear();
    updateRecentList();
}

void PageScripting::on_openExampleButton_clicked()
{
    auto *item = ui->exampleList->currentItem();

    if (item) {
        QFile file(item->data(Qt::UserRole).toString());

        if (!file.open(QIODevice::ReadOnly)) {
            QMessageBox::critical(this, "Open QML File",
                                  "Could not open example for reading");
            return;
        }

        ui->qmlEdit->setPlainText(file.readAll());
        ui->fileNowLabel->setText("");

        file.close();
    } else {
        QMessageBox::critical(this, "Open Example",
                              "Please select one example.");
    }
}

void PageScripting::updateRecentList()
{
    ui->recentList->clear();
    for (auto f: mRecentFiles) {
        ui->recentList->addItem(f);
    }
}

void PageScripting::on_helpButton_clicked()
{
    QString html = "<b>Keyboard Commands</b><br>"
                   "Ctrl + '+'   : Increase font size<br>"
                   "Ctrl + '-'   : Decrease font size<br>"
                   "Ctrl + space : Show auto-complete suggestions<br>"
                   "Ctrl + '/'   : Toggle auto-comment on line or block<br>"
                   "Ctrl + 'i'   : Auto-indent selected line or block<br>"
                   "Ctrl + 'f'   : Open search (and replace) bar<br>"
                   "Ctrl + 'e'   : Run or restart embedded<br>"
                   "Ctrl + 'w'   : Run or restart window<br>"
                   "Ctrl + 'q'   : Stop code<br>"
                   "Ctrl + 'd'   : Clear console<br>"
                   "Ctrl + 's'   : Save file<br>";

    HelpDialog::showHelpMonospace(this, "VESC Tool Script Editor", html.replace(" ","&nbsp;"));
}

void PageScripting::on_searchEdit_textChanged(const QString &arg1)
{
    ui->qmlEdit->searchForString(arg1);
}

void PageScripting::on_searchHideButton_clicked()
{
    ui->searchWidget->setVisible(false);
    ui->qmlEdit->searchForString("");
}

void PageScripting::on_searchNextButton_clicked()
{
    ui->qmlEdit->searchNextResult();
    ui->qmlEdit->setFocus();
}

void PageScripting::on_replaceThisButton_clicked()
{
    if (!ui->qmlEdit->textCursor().selectedText().isEmpty()) {
        ui->qmlEdit->textCursor().insertText(ui->replaceEdit->text());
        ui->qmlEdit->searchNextResult();
    }
}

void PageScripting::on_replaceAllButton_clicked()
{
    ui->qmlEdit->searchNextResult();
    while (!ui->qmlEdit->textCursor().selectedText().isEmpty()) {
        ui->qmlEdit->textCursor().insertText(ui->replaceEdit->text());
        ui->qmlEdit->searchNextResult();
    }
}
