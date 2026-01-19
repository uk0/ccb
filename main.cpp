#include "mainwindow.h"
#include "licensemanager.h"
#include "licensedialog.h"

#include <QApplication>
#include <QLoggingCategory>
#include <QDir>
#include <QStandardPaths>
#include <QMessageBox>

int main(int argc, char *argv[])
{
    // Suppress macOS IMK warnings
    QLoggingCategory::setFilterRules("qt.qpa.input.methods.warning=false");

    // Change working directory to home to avoid Desktop folder access
    QDir::setCurrent(QDir::homePath());

    QApplication a(argc, argv);

    // Use simple names for paths - avoid spaces
    a.setOrganizationName("ccb");
    a.setOrganizationDomain("firsh.me");
    a.setApplicationName("ccb");
    a.setApplicationVersion("1.0");

    // License check
    LicenseManager licenseManager;

    // Try to load existing license
    bool hasValidLicense = licenseManager.loadLicense();

    // If no valid license, show activation dialog
    if (!hasValidLicense) {
        LicenseDialog dialog(&licenseManager);
        if (dialog.exec() != QDialog::Accepted) {
            // User cancelled or failed to activate
            return 0;
        }
    } else {
        // Check if license is about to expire (within 7 days)
        int daysRemaining = licenseManager.getDaysRemaining();
        if (daysRemaining <= 7 && daysRemaining > 0) {
            QMessageBox::warning(nullptr, "License Expiring",
                                 QString("Your license will expire in %1 days.\n\n"
                                         "Please renew your license to continue using the application.")
                                     .arg(daysRemaining));
        }
    }

    MainWindow w(&licenseManager);
    w.show();
    return a.exec();
}
