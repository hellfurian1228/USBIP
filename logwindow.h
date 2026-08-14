#ifndef LOGWINDOW_H
#define LOGWINDOW_H

#include <QDialog>
#include <QTextEdit>
#include <QPushButton>
#include <QComboBox>
#include <QVBoxLayout>
#include <QHBoxLayout>

class LogWindow : public QDialog {
    Q_OBJECT
public:
    explicit LogWindow(QWidget *parent = nullptr);
    void appendLog(const QString &level, const QString &message);

private slots:
    void clearLogs();
    void copyLogs();

private:
    QTextEdit *logTextEdit;
    QComboBox *filterComboBox;
    QPushButton *clearButton;
    QPushButton *copyButton;
};

#endif // LOGWINDOW_H
