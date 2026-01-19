#include "licensemanager.h"

#include <QCryptographicHash>
#include <QNetworkInterface>
#include <QSysInfo>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QRandomGenerator>
#include <QSettings>

// Platform-specific includes for hardware UUID
#ifdef Q_OS_MACOS
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#endif

// Secret keys - in production, these should be obfuscated
const QByteArray LicenseManager::SECRET_KEY = "CCB_LICENSE_KEY_2024_FIRSH_ME";
const QByteArray LicenseManager::STORAGE_KEY = "CCB_STORAGE_ENC_KEY_UK0";
const QString LicenseManager::LICENSE_MAGIC = "CCBL";

LicenseManager::LicenseManager(QObject *parent)
    : QObject(parent)
    , m_isValid(false)
{
    m_machineId = generateMachineId();
}

/**
 * Check if a MAC address belongs to a virtual network adapter.
 * Virtual adapters should be excluded for stable machine identification.
 */
bool LicenseManager::isVirtualMacAddress(const QString &mac) const
{
    if (mac.isEmpty()) return true;

    // Normalize MAC format: remove separators, uppercase
    QString normalized = mac.toUpper();
    normalized.remove(':').remove('-').remove('.');

    if (normalized.length() < 6) return true;

    // Get OUI (first 6 characters = 3 bytes)
    QString oui = normalized.left(6);

    // Known virtual adapter OUI prefixes
    static const QStringList virtualOuis = {
        // VMware
        "005056", "000C29", "001C14", "000569",
        // Microsoft Hyper-V
        "00155D",
        // VirtualBox
        "080027", "0A0027",
        // Docker / Container
        "0242AC", "024276", "02420A",
        // Xen
        "00163E",
        // Parallels
        "001C42",
        // QEMU/KVM
        "525400", "001122",
        // Virtual Iron
        "00174F",
        // Amazon EC2
        "0A5886",
    };

    for (const QString &prefix : virtualOuis) {
        if (oui.startsWith(prefix)) {
            return true;
        }
    }

    // Check for locally administered address (bit 1 of first byte set)
    // These are often generated randomly for virtual adapters
    bool ok;
    int firstByte = normalized.left(2).toInt(&ok, 16);
    if (ok && (firstByte & 0x02)) {
        // Locally administered - could be virtual, but not definitive
        // We'll allow it as some real adapters use this too
    }

    return false;
}

/**
 * Get the first physical (non-virtual) MAC address.
 * Returns empty string if no suitable adapter found.
 */
QString LicenseManager::getPhysicalMacAddress() const
{
    QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();

    // First pass: look for active, non-virtual adapters
    for (const QNetworkInterface &iface : interfaces) {
        // Skip loopback
        if (iface.flags() & QNetworkInterface::IsLoopBack) continue;

        // Skip down interfaces
        if (!(iface.flags() & QNetworkInterface::IsUp)) continue;

        QString mac = iface.hardwareAddress();

        // Skip empty or null MAC
        if (mac.isEmpty() || mac == "00:00:00:00:00:00") continue;

        // Skip virtual adapters
        if (isVirtualMacAddress(mac)) continue;

        return mac;
    }

    // Second pass: try any non-loopback adapter
    for (const QNetworkInterface &iface : interfaces) {
        if (iface.flags() & QNetworkInterface::IsLoopBack) continue;

        QString mac = iface.hardwareAddress();
        if (mac.isEmpty() || mac == "00:00:00:00:00:00") continue;
        if (isVirtualMacAddress(mac)) continue;

        return mac;
    }

    return QString();
}

/**
 * Get platform-specific hardware UUID.
 * - macOS: IOPlatformUUID (stable across OS updates)
 * - Windows: Combination of BIOS info + MachineGuid (stable across updates)
 */
QString LicenseManager::getPlatformHardwareUUID() const
{
#ifdef Q_OS_MACOS
    // macOS: Get IOPlatformUUID from IOKit
    // This is the hardware UUID stored in NVRAM, extremely stable
    QString uuid;

    io_service_t platformExpert = IOServiceGetMatchingService(
        kIOMainPortDefault,
        IOServiceMatching("IOPlatformExpertDevice")
    );

    if (platformExpert) {
        CFTypeRef uuidRef = IORegistryEntryCreateCFProperty(
            platformExpert,
            CFSTR("IOPlatformUUID"),
            kCFAllocatorDefault,
            0
        );

        if (uuidRef) {
            if (CFGetTypeID(uuidRef) == CFStringGetTypeID()) {
                char buffer[128];
                if (CFStringGetCString((CFStringRef)uuidRef, buffer, sizeof(buffer), kCFStringEncodingUTF8)) {
                    uuid = QString::fromUtf8(buffer);
                }
            }
            CFRelease(uuidRef);
        }
        IOObjectRelease(platformExpert);
    }

    return uuid;

#elif defined(Q_OS_WIN)
    // Windows: Combine multiple sources for stability
    // 1. BIOS/System info from registry (hardware-based)
    // 2. MachineGuid as fallback

    QString hwInfo;

    // Try to get BIOS information (stable, hardware-based)
    QSettings biosReg("HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\BIOS",
                      QSettings::NativeFormat);
    QString biosVendor = biosReg.value("BIOSVendor").toString();
    QString systemProduct = biosReg.value("SystemProductName").toString();
    QString systemSerial = biosReg.value("SystemSerialNumber").toString();

    if (!systemProduct.isEmpty()) {
        hwInfo += systemProduct;
    }
    if (!systemSerial.isEmpty() && systemSerial != "System Serial Number"
        && systemSerial != "To be filled by O.E.M.") {
        hwInfo += systemSerial;
    }
    if (!biosVendor.isEmpty()) {
        hwInfo += biosVendor;
    }

    // Add MachineGuid (changes on reinstall, but stable across updates)
    QSettings cryptoReg("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Cryptography",
                        QSettings::NativeFormat);
    QString machineGuid = cryptoReg.value("MachineGuid").toString();
    if (!machineGuid.isEmpty()) {
        hwInfo += machineGuid;
    }

    return hwInfo;

#else
    // Other platforms: use Qt's machineUniqueId
    return QString::fromUtf8(QSysInfo::machineUniqueId());
#endif
}

/**
 * Generate a stable machine ID that:
 * - Does NOT change with OS version updates
 * - Is unique per machine
 * - Is secure (hash-based, hard to predict)
 *
 * Algorithm:
 * SHA256(PlatformHardwareUUID + PhysicalMAC + PlatformID + VersionedSalt)
 */
QString LicenseManager::generateMachineId() const
{
    QByteArray hwInfo;
    bool hasStableId = false;

    // 1. Platform-specific hardware UUID (most stable)
    //    - macOS: IOPlatformUUID (never changes unless NVRAM reset)
    //    - Windows: BIOS info + MachineGuid (stable across updates)
    QString platformUUID = getPlatformHardwareUUID();
    if (!platformUUID.isEmpty()) {
        hwInfo.append(platformUUID.toUtf8());
        hasStableId = true;
    }

    // 2. First physical MAC address (skip virtual adapters)
    QString mac = getPhysicalMacAddress();
    if (!mac.isEmpty()) {
        hwInfo.append(mac.toUtf8());
        hasStableId = true;
    }

    // 3. Fallback: use Qt's machineUniqueId if nothing else available
    if (!hasStableId) {
        QByteArray qtId = QSysInfo::machineUniqueId();
        if (!qtId.isEmpty()) {
            hwInfo.append(qtId);
        }
        // Last resort: use hostname (less stable but better than nothing)
        else {
            hwInfo.append(QSysInfo::machineHostName().toUtf8());
        }
    }

    // 4. Platform identifier (NOT version, just platform type)
    //    This prevents cross-platform license transfer
#if defined(Q_OS_MACOS)
    hwInfo.append("PLATFORM:MACOS");
#elif defined(Q_OS_WIN)
    hwInfo.append("PLATFORM:WINDOWS");
#elif defined(Q_OS_LINUX)
    hwInfo.append("PLATFORM:LINUX");
#else
    hwInfo.append("PLATFORM:UNKNOWN");
#endif

    // 5. Versioned salt (allows algorithm migration in future)
    //    V3 = current stable version without OS version dependency
    hwInfo.append("CCB_HWID_V3_STABLE_2024");

    // Generate SHA256 hash
    QByteArray hash = QCryptographicHash::hash(hwInfo, QCryptographicHash::Sha256);
    QString idHex = hash.toHex().left(32).toUpper();

    // Format as XXXXXXXX-XXXXXXXX-XXXXXXXX-XXXXXXXX
    QString formatted;
    for (int i = 0; i < 32; i += 8) {
        if (i > 0) formatted += "-";
        formatted += idHex.mid(i, 8);
    }

    return formatted;
}

QString LicenseManager::getMachineId() const
{
    return m_machineId;
}

QString LicenseManager::getLicenseFilePath() const
{
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (configDir.isEmpty()) {
        configDir = QDir::homePath() + "/.ccb";
    }
    QDir().mkpath(configDir);
    return configDir + "/license.dat";
}

QByteArray LicenseManager::xorCrypt(const QByteArray &data, const QByteArray &key) const
{
    QByteArray result;
    result.resize(data.size());
    for (int i = 0; i < data.size(); ++i) {
        result[i] = data[i] ^ key[i % key.size()];
    }
    return result;
}

QString LicenseManager::base32Encode(const QByteArray &data) const
{
    // Custom Base32 alphabet (32 unique characters, avoiding I, L, O which are confusing)
    static const char alphabet[] = "ABCDEFGHJKMNPQRSTUVWXYZ234567890";

    QString result;
    int buffer = 0;
    int bitsLeft = 0;

    for (int i = 0; i < data.size(); ++i) {
        buffer = (buffer << 8) | (static_cast<unsigned char>(data[i]));
        bitsLeft += 8;
        while (bitsLeft >= 5) {
            bitsLeft -= 5;
            int index = (buffer >> bitsLeft) & 0x1F;
            result += alphabet[index];
            buffer &= (1 << bitsLeft) - 1;
        }
    }

    if (bitsLeft > 0) {
        int index = (buffer << (5 - bitsLeft)) & 0x1F;
        result += alphabet[index];
    }

    return result;
}

QByteArray LicenseManager::base32Decode(const QString &encoded) const
{
    static const char alphabet[] = "ABCDEFGHJKMNPQRSTUVWXYZ234567890";
    static QMap<QChar, int> decodeMap;
    if (decodeMap.isEmpty()) {
        for (int i = 0; i < 32; ++i) {
            decodeMap[QChar(alphabet[i])] = i;
        }
    }

    QByteArray result;
    int buffer = 0;
    int bitsLeft = 0;

    for (const QChar &c : encoded) {
        if (!decodeMap.contains(c.toUpper())) continue;
        buffer = (buffer << 5) | decodeMap[c.toUpper()];
        bitsLeft += 5;
        while (bitsLeft >= 8) {
            bitsLeft -= 8;
            result.append(static_cast<char>((buffer >> bitsLeft) & 0xFF));
            buffer &= (1 << bitsLeft) - 1;
        }
    }

    return result;
}

bool LicenseManager::decodeLicense(const QString &licenseKey, QString &machineId, QDate &expiration)
{
    // Remove dashes and spaces
    QString cleanKey = licenseKey;
    cleanKey.remove('-').remove(' ');

    if (cleanKey.length() < 20) {
        m_lastError = "License key too short";
        return false;
    }

    // Decode Base32
    QByteArray decoded = base32Decode(cleanKey);
    if (decoded.size() < 16) {
        m_lastError = "Invalid license format";
        return false;
    }

    // Structure: [machineIdHash:8][expireDate:4][signature:4]
    // Total: 16 bytes minimum

    QByteArray machineIdHash = decoded.left(8);
    QByteArray expireDateBytes = decoded.mid(8, 4);
    QByteArray signature = decoded.mid(12, 4);

    // Extract expiration date (days since epoch)
    quint32 expireDays = 0;
    for (int i = 0; i < 4; ++i) {
        expireDays = (expireDays << 8) | static_cast<unsigned char>(expireDateBytes[i]);
    }
    expiration = QDate(1970, 1, 1).addDays(expireDays);

    // Verify signature
    QByteArray signData = machineIdHash + expireDateBytes;
    QByteArray expectedSig = QCryptographicHash::hash(signData + SECRET_KEY, QCryptographicHash::Sha256).left(4);

    if (signature != expectedSig) {
        m_lastError = "Invalid license signature";
        return false;
    }

    // Verify machine ID
    QString rawMachineId = m_machineId;
    rawMachineId.remove('-');
    QByteArray expectedMachineHash = QCryptographicHash::hash(
        rawMachineId.toUtf8() + SECRET_KEY, QCryptographicHash::Sha256).left(8);

    if (machineIdHash != expectedMachineHash) {
        m_lastError = "License not valid for this machine";
        return false;
    }

    machineId = m_machineId;
    return true;
}

bool LicenseManager::activateLicense(const QString &licenseKey)
{
    QString machineId;
    QDate expiration;

    if (!decodeLicense(licenseKey, machineId, expiration)) {
        m_isValid = false;
        return false;
    }

    // Check expiration
    if (expiration < QDate::currentDate()) {
        m_lastError = QString("License expired on %1").arg(expiration.toString("yyyy-MM-dd"));
        m_isValid = false;
        return false;
    }

    // Save license
    if (!saveLicense(licenseKey)) {
        m_lastError = "Failed to save license";
        return false;
    }

    m_licenseKey = licenseKey;
    m_expirationDate = expiration;
    m_isValid = true;
    m_lastError.clear();

    emit licenseActivated();
    return true;
}

bool LicenseManager::saveLicense(const QString &licenseKey)
{
    QJsonObject obj;
    obj["license"] = licenseKey;
    obj["machineId"] = m_machineId;
    obj["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QJsonDocument doc(obj);
    QByteArray data = doc.toJson(QJsonDocument::Compact);

    // Encrypt
    QByteArray encrypted = xorCrypt(data, STORAGE_KEY);
    QByteArray encoded = LICENSE_MAGIC.toUtf8() + encrypted.toBase64();

    QFile file(getLicenseFilePath());
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    file.write(encoded);
    file.close();
    return true;
}

bool LicenseManager::loadLicense()
{
    QString filePath = getLicenseFilePath();
    QFile file(filePath);

    if (!file.exists()) {
        m_lastError = "No license file found";
        m_isValid = false;
        return false;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        m_lastError = "Cannot read license file";
        m_isValid = false;
        return false;
    }

    QByteArray encoded = file.readAll();
    file.close();

    // Check magic
    if (!encoded.startsWith(LICENSE_MAGIC.toUtf8())) {
        m_lastError = "Invalid license file";
        m_isValid = false;
        return false;
    }

    // Decode and decrypt
    QByteArray base64Data = encoded.mid(LICENSE_MAGIC.length());
    QByteArray encrypted = QByteArray::fromBase64(base64Data);
    QByteArray decrypted = xorCrypt(encrypted, STORAGE_KEY);

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(decrypted, &error);
    if (error.error != QJsonParseError::NoError) {
        m_lastError = "Corrupted license file";
        m_isValid = false;
        return false;
    }

    QJsonObject obj = doc.object();
    QString savedLicense = obj["license"].toString();
    QString savedMachineId = obj["machineId"].toString();

    // Verify machine ID matches
    if (savedMachineId != m_machineId) {
        m_lastError = "License is for a different machine";
        m_isValid = false;
        return false;
    }

    // Validate the license
    return activateLicense(savedLicense);
}

bool LicenseManager::isLicenseValid() const
{
    if (!m_isValid) return false;
    if (m_expirationDate < QDate::currentDate()) return false;
    return true;
}

QDate LicenseManager::getExpirationDate() const
{
    return m_expirationDate;
}

int LicenseManager::getDaysRemaining() const
{
    if (!m_isValid) return 0;
    return QDate::currentDate().daysTo(m_expirationDate);
}

bool LicenseManager::deleteLicense()
{
    QString filePath = getLicenseFilePath();
    QFile file(filePath);

    if (file.exists()) {
        if (!file.remove()) {
            m_lastError = "Failed to delete license file";
            return false;
        }
    }

    // Reset state
    m_licenseKey.clear();
    m_expirationDate = QDate();
    m_isValid = false;
    m_lastError.clear();

    return true;
}
