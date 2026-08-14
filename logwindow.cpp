#include "logwindow.h"
#include <QDateTime>
#include <QGuiApplication>
#include <QClipboard>

LogWindow::LogWindow(QWidget *parent) : QDialog(parent) {
    setWindowTitle("USBIP Client System Logger");
    resize(700, 450);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    QHBoxLayout *topBarLayout = new QHBoxLayout();
    filterComboBox = new QComboBox(this);
    filterComboBox->addItems({"All Logs", "INFO", "WARNING", "ERROR"});
    
    clearButton = new QPushButton("Clear Logs", this);
    copyButton = new QPushButton("Copy to Clipboard", this);

    topBarLayout->addWidget(filterComboBox);
    topBarLayout->addStretch();
    topBarLayout->addWidget(copyButton);
    topBarLayout->addWidget(clearButton);

    logTextEdit = new QTextEdit(this);
    logTextEdit->setReadOnly(true);
    logTextEdit->setStyleSheet("font-family: 'Consolas', 'Courier New', monospace; font-size: 12px; background-color: #0d0e15; color: #00ffcc;");

    mainLayout->addLayout(topBarLayout);
    mainLayout->addWidget(logTextEdit);

    connect(clearButton, &QPushButton::clicked, this, &LogWindow::clearLogs);
    connect(copyButton, &QPushButton::clicked, this, &LogWindow::copyLogs);
}

void LogWindow::appendLog(const QString &level, const QString &message) {
    QString timeStr = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
    QString color = "#00ffcc";
    if (level == "WARNING") color = "#ffaa00";
    if (level == "ERROR") color = "#ff3366";

    QString formattedEntry = QString("<span style='color:#777777;'>[%1]</span> <b style='color:%2;'>[%3]</b> %4")
                                .arg(timeStr, color, level, message);
    logTextEdit->append(formattedEntry);
}

void LogWindow::clearLogs() {
    logTextEdit->clear();
}

void LogWindow::copyLogs() {
    QClipboard *clipboard = QGuiApplication::clipboard();
    clipboard->setText(logTextEdit->toPlainText());
}
