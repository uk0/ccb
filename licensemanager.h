#ifndef LICENSEMANAGER_H
#define LICENSEMANAGER_H

#include <QObject>
#include <QString>
#include <QDate>

class LicenseManager : public QObject
{
    Q_OBJECT

public:
    explicit LicenseManager(QObject *parent = nullptr);

    // Get unique machine ID
    QString getMachineId() const;

    // Validate and activate license
    bool activateLicense(const QString &licenseKey);

    // Check if license is valid
    bool isLicenseValid() const;

    // Get license expiration date
    QDate getExpirationDate() const;

    // Get days remaining
    int getDaysRemaining() const;

    // Get license file path
    QString getLicenseFilePath() const;

    // Load saved license
    bool loadLicense();

    // Delete license file
    bool deleteLicense();

    // Get last error message
    QString getLastError() const { return m_lastError; }

signals:
    void licenseActivated();
    void licenseExpired();

private:
    // Generate machine ID from hardware info
    QString generateMachineId() const;

    // Platform-specific hardware UUID
    QString getPlatformHardwareUUID() const;

    // Get first physical (non-virtual) MAC address
    QString getPhysicalMacAddress() const;

    // Check if MAC address is from virtual adapter
    bool isVirtualMacAddress(const QString &mac) const;

    // Decode and validate license key
    bool decodeLicense(const QString &licenseKey, QString &machineId, QDate &expiration);

    // Save license to file
    bool saveLicense(const QString &licenseKey);

    // XOR encrypt/decrypt
    QByteArray xorCrypt(const QByteArray &data, const QByteArray &key) const;

    // Custom Base32 encode/decode
    QString base32Encode(const QByteArray &data) const;
    QByteArray base32Decode(const QString &encoded) const;

    QString m_machineId;
    QString m_licenseKey;
    QDate m_expirationDate;
    bool m_isValid;
    QString m_lastError;

    // Secret keys (obfuscated in real implementation)
    static const QByteArray SECRET_KEY;
    static const QByteArray STORAGE_KEY;
    static const QString LICENSE_MAGIC;
};

#endif // LICENSEMANAGER_H
