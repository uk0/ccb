#include "licensedialog.h"
#include "licensemanager.h"
#include "macosstylemanager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QClipboard>
#include <QApplication>
#include <QMessageBox>
#include <QFont>
#include <QFrame>
#include <QTimer>
#include <QSpacerItem>

LicenseDialog::LicenseDialog(LicenseManager *manager, QWidget *parent)
    : QDialog(parent)
    , m_licenseManager(manager)
    , m_activated(false)
{
    setWindowTitle("License Activation");
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setFixedSize(480, 380);
    setModal(true);

    // Apply macOS style
    MacOSStyleManager::instance().applyToDialog(this);

    setupUi();
    updateStatus();
}

void LicenseDialog::setupUi()
{
    auto &style = MacOSStyleManager::instance();

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);
    mainLayout->setContentsMargins(20, 16, 20, 16);

    // Title - compact
    QLabel *titleLabel = new QLabel("Claude Code Balance");
    titleLabel->setStyleSheet(QString("font-size: 18px; font-weight: 600; color: %1;")
        .arg(style.textColor().name()));
    titleLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(titleLabel);

    // Machine ID Group - compact
    QGroupBox *machineGroup = new QGroupBox("MACHINE ID");
    QVBoxLayout *machineLayout = new QVBoxLayout(machineGroup);
    machineLayout->setSpacing(6);
    machineLayout->setContentsMargins(10, 12, 10, 10);

    QHBoxLayout *machineIdLayout = new QHBoxLayout;
    machineIdLayout->setSpacing(6);

    m_machineIdEdit = new QLineEdit;
    m_machineIdEdit->setReadOnly(true);
    m_machineIdEdit->setText(m_licenseManager->getMachineId());
    m_machineIdEdit->setFont(QFont("SF Mono", 11));
    m_machineIdEdit->setFixedHeight(30);

    m_copyBtn = new QPushButton("Copy");
    m_copyBtn->setFixedSize(60, 30);
    connect(m_copyBtn, &QPushButton::clicked, this, &LicenseDialog::onCopyMachineId);

    machineIdLayout->addWidget(m_machineIdEdit, 1);
    machineIdLayout->addWidget(m_copyBtn);
    machineLayout->addLayout(machineIdLayout);

    mainLayout->addWidget(machineGroup);

    // License Key Group - compact
    QGroupBox *licenseGroup = new QGroupBox("LICENSE KEY");
    QVBoxLayout *licenseLayout = new QVBoxLayout(licenseGroup);
    licenseLayout->setSpacing(8);
    licenseLayout->setContentsMargins(10, 12, 10, 10);

    m_licenseEdit = new QLineEdit;
    m_licenseEdit->setPlaceholderText("XXXXX-XXXXX-XXXXX-XXXXX-XXXXX-X");
    m_licenseEdit->setFont(QFont("SF Mono", 11));
    m_licenseEdit->setFixedHeight(30);
    licenseLayout->addWidget(m_licenseEdit);

    m_activateBtn = new QPushButton("Activate");
    m_activateBtn->setFixedHeight(32);
    m_activateBtn->setStyleSheet(style.getAccentButtonStyle());
    connect(m_activateBtn, &QPushButton::clicked, this, &LicenseDialog::onActivateClicked);
    licenseLayout->addWidget(m_activateBtn);

    mainLayout->addWidget(licenseGroup);

    // Status Group - compact
    QGroupBox *statusGroup = new QGroupBox("STATUS");
    QVBoxLayout *statusLayout = new QVBoxLayout(statusGroup);
    statusLayout->setSpacing(4);
    statusLayout->setContentsMargins(10, 12, 10, 10);

    m_statusLabel = new QLabel("Not activated");
    m_statusLabel->setStyleSheet(QString("font-weight: 500; font-size: 12px; color: %1;")
        .arg(style.dangerColor().name()));
    statusLayout->addWidget(m_statusLabel);

    m_expirationLabel = new QLabel("");
    m_expirationLabel->setStyleSheet(QString("color: %1; font-size: 11px;")
        .arg(style.secondaryTextColor().name()));
    statusLayout->addWidget(m_expirationLabel);

    mainLayout->addWidget(statusGroup);

    // Spacer
    mainLayout->addStretch(1);

    // Bottom Button
    QHBoxLayout *buttonLayout = new QHBoxLayout;
    buttonLayout->addStretch(1);

    m_exitBtn = new QPushButton("Exit");
    m_exitBtn->setFixedSize(90, 30);
    connect(m_exitBtn, &QPushButton::clicked, this, &LicenseDialog::onExitClicked);
    buttonLayout->addWidget(m_exitBtn);

    mainLayout->addLayout(buttonLayout);
}

void LicenseDialog::updateStatus()
{
    auto &style = MacOSStyleManager::instance();

    if (m_licenseManager->isLicenseValid()) {
        m_statusLabel->setText("Activated");
        m_statusLabel->setStyleSheet(QString("font-weight: 500; font-size: 12px; color: %1;")
            .arg(style.successColor().name()));

        int days = m_licenseManager->getDaysRemaining();
        QDate expDate = m_licenseManager->getExpirationDate();
        m_expirationLabel->setText(QString("Expires: %1 (%2 days)")
                                       .arg(expDate.toString("yyyy-MM-dd"))
                                       .arg(days));

        if (days <= 7) {
            m_expirationLabel->setStyleSheet(QString("color: %1; font-size: 11px;")
                .arg(style.warningColor().name()));
        } else {
            m_expirationLabel->setStyleSheet(QString("color: %1; font-size: 11px;")
                .arg(style.secondaryTextColor().name()));
        }

        m_activated = true;
        m_activateBtn->setText("Re-Activate");
        m_exitBtn->setText("Continue");
    } else {
        m_statusLabel->setText("Not activated");
        m_statusLabel->setStyleSheet(QString("font-weight: 500; font-size: 12px; color: %1;")
            .arg(style.dangerColor().name()));

        QString error = m_licenseManager->getLastError();
        if (!error.isEmpty()) {
            m_expirationLabel->setText(error);
            m_expirationLabel->setStyleSheet(QString("color: %1; font-size: 11px;")
                .arg(style.dangerColor().name()));
        } else {
            m_expirationLabel->setText("Enter a valid license key");
            m_expirationLabel->setStyleSheet(QString("color: %1; font-size: 11px;")
                .arg(style.secondaryTextColor().name()));
        }

        m_activated = false;
        m_activateBtn->setText("Activate");
        m_exitBtn->setText("Exit");
    }
}

void LicenseDialog::onActivateClicked()
{
    QString licenseKey = m_licenseEdit->text().trimmed();

    if (licenseKey.isEmpty()) {
        QMessageBox::warning(this, "Error", "Please enter a license key.");
        return;
    }

    if (m_licenseManager->activateLicense(licenseKey)) {
        QMessageBox::information(this, "Success",
                                 QString("License activated successfully!\n\nExpires: %1")
                                     .arg(m_licenseManager->getExpirationDate().toString("yyyy-MM-dd")));
        updateStatus();
    } else {
        QMessageBox::critical(this, "Activation Failed",
                              QString("Failed to activate license:\n\n%1")
                                  .arg(m_licenseManager->getLastError()));
        updateStatus();
    }
}

void LicenseDialog::onCopyMachineId()
{
    auto &style = MacOSStyleManager::instance();

    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(m_machineIdEdit->text());

    m_copyBtn->setText("OK");
    m_copyBtn->setStyleSheet(style.getAccentButtonStyle());

    QTimer::singleShot(1200, this, [this]() {
        m_copyBtn->setText("Copy");
        m_copyBtn->setStyleSheet(MacOSStyleManager::instance().getPrimaryButtonStyle());
    });
}

void LicenseDialog::onExitClicked()
{
    if (m_activated) {
        accept();
    } else {
        reject();
    }
}
