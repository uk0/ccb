#ifndef LICENSEDIALOG_H
#define LICENSEDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

class LicenseManager;

class LicenseDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LicenseDialog(LicenseManager *manager, QWidget *parent = nullptr);

    bool isActivated() const { return m_activated; }

private slots:
    void onActivateClicked();
    void onCopyMachineId();
    void onExitClicked();

private:
    void setupUi();
    void updateStatus();

    LicenseManager *m_licenseManager;
    QLabel *m_machineIdLabel;
    QLineEdit *m_machineIdEdit;
    QPushButton *m_copyBtn;
    QLineEdit *m_licenseEdit;
    QPushButton *m_activateBtn;
    QPushButton *m_exitBtn;
    QLabel *m_statusLabel;
    QLabel *m_expirationLabel;
    bool m_activated;
};

#endif // LICENSEDIALOG_H
