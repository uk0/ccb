#include "macosstylemanager.h"

#include <QApplication>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>
#include <QTimer>
#include <QWidget>
#include <QWindow>

#ifdef Q_OS_MACOS
#include <QProcess>
#endif

MacOSStyleManager& MacOSStyleManager::instance()
{
    static MacOSStyleManager instance;
    return instance;
}

MacOSStyleManager::MacOSStyleManager(QObject *parent)
    : QObject(parent)
{
    updateColors();
}

void MacOSStyleManager::initialize(QWidget *mainWindow)
{
    m_mainWindow = mainWindow;

    // Detect system theme
    m_effectiveTheme = (m_theme == System) ? detectSystemTheme() : m_theme;
    updateColors();

    // Apply initial style
    if (m_mainWindow) {
        applyToMainWindow(m_mainWindow);
    }

    // Set up periodic theme check
    QTimer *themeCheckTimer = new QTimer(this);
    connect(themeCheckTimer, &QTimer::timeout, this, &MacOSStyleManager::refreshTheme);
    themeCheckTimer->start(2000);
}

MacOSStyleManager::Theme MacOSStyleManager::detectSystemTheme() const
{
#ifdef Q_OS_MACOS
    QProcess process;
    process.start("defaults", {"read", "-g", "AppleInterfaceStyle"});
    process.waitForFinished(500);
    QString output = process.readAllStandardOutput().trimmed();
    return (output.toLower() == "dark") ? Dark : Light;
#else
    QPalette palette = QApplication::palette();
    QColor windowColor = palette.color(QPalette::Window);
    return (windowColor.lightness() < 128) ? Dark : Light;
#endif
}

MacOSStyleManager::Theme MacOSStyleManager::currentTheme() const
{
    return m_effectiveTheme;
}

bool MacOSStyleManager::isDarkMode() const
{
    return m_effectiveTheme == Dark;
}

void MacOSStyleManager::setTheme(Theme theme)
{
    m_theme = theme;
    Theme newEffective = (theme == System) ? detectSystemTheme() : theme;

    if (newEffective != m_effectiveTheme) {
        m_effectiveTheme = newEffective;
        updateColors();

        if (m_mainWindow) {
            applyToMainWindow(m_mainWindow);
        }

        emit themeChanged(m_effectiveTheme);
    }
}

void MacOSStyleManager::refreshTheme()
{
    if (m_theme != System) return;

    Theme detected = detectSystemTheme();
    if (detected != m_effectiveTheme) {
        m_effectiveTheme = detected;
        updateColors();

        if (m_mainWindow) {
            applyToMainWindow(m_mainWindow);
        }

        emit themeChanged(m_effectiveTheme);
    }
}

void MacOSStyleManager::updateColors()
{
    if (m_effectiveTheme == Dark) {
        // macOS 26 Tahoe Dark Mode - Frosted Glass Style
        // More translucent, unified gray tones
        m_backgroundColor = QColor(28, 28, 30);               // Main window bg
        m_secondaryBackgroundColor = QColor(44, 44, 46);      // Cards/panels - slightly lighter
        m_tertiaryBackgroundColor = QColor(54, 54, 56);       // Input fields
        m_textColor = QColor(255, 255, 255, 230);             // Primary text (slightly transparent)
        m_secondaryTextColor = QColor(174, 174, 178);         // Secondary text
        m_accentColor = QColor(10, 132, 255);                 // System blue
        m_borderColor = QColor(255, 255, 255, 20);            // Very subtle borders
        m_separatorColor = QColor(255, 255, 255, 15);         // Almost invisible separators
        m_successColor = QColor(48, 209, 88);
        m_dangerColor = QColor(255, 69, 58);
        m_warningColor = QColor(255, 159, 10);
    } else {
        // macOS 26 Tahoe Light Mode - Frosted Glass Style
        m_backgroundColor = QColor(246, 246, 246);
        m_secondaryBackgroundColor = QColor(255, 255, 255, 200);
        m_tertiaryBackgroundColor = QColor(255, 255, 255);
        m_textColor = QColor(0, 0, 0, 220);
        m_secondaryTextColor = QColor(99, 99, 102);
        m_accentColor = QColor(0, 122, 255);
        m_borderColor = QColor(0, 0, 0, 15);
        m_separatorColor = QColor(0, 0, 0, 10);
        m_successColor = QColor(40, 205, 65);
        m_dangerColor = QColor(255, 59, 48);
        m_warningColor = QColor(255, 149, 0);
    }
}

QString MacOSStyleManager::colorToRgba(const QColor &color, int alpha) const
{
    return QString("rgba(%1, %2, %3, %4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(alpha / 255.0, 0, 'f', 2);
}

void MacOSStyleManager::applyToWidget(QWidget *widget)
{
    if (!widget) return;
    widget->setStyleSheet(getFullStyleSheet());
}

void MacOSStyleManager::applyToMainWindow(QWidget *mainWindow)
{
    if (!mainWindow) return;
    mainWindow->setStyleSheet(getFullStyleSheet());
}

void MacOSStyleManager::applyToDialog(QWidget *dialog)
{
    if (!dialog) return;
    dialog->setStyleSheet(getFullStyleSheet());
}

QString MacOSStyleManager::getFullStyleSheet() const
{
    return getMainWindowStyle() +
           getGroupBoxStyle() +
           getPrimaryButtonStyle() +
           getLineEditStyle() +
           getSpinBoxStyle() +
           getCheckBoxStyle() +
           getListWidgetStyle() +
           getPlainTextEditStyle() +
           getLabelStyle() +
           getSplitterStyle() +
           getMenuBarStyle() +
           getStatusBarStyle() +
           getScrollBarStyle() +
           getToolTipStyle();
}

QString MacOSStyleManager::getMainWindowStyle() const
{
    return QString(
        "QMainWindow, QDialog {"
        "    background-color: %1;"
        "}"
        "QWidget {"
        "    font-family: -apple-system, 'SF Pro Text', 'Helvetica Neue', sans-serif;"
        "    font-size: 13px;"
        "}"
    ).arg(m_backgroundColor.name());
}

QString MacOSStyleManager::getDialogStyle() const
{
    return QString(
        "QDialog {"
        "    background-color: %1;"
        "}"
    ).arg(m_backgroundColor.name());
}

QString MacOSStyleManager::getGroupBoxStyle() const
{
    // macOS 26 style: minimal borders, subtle background difference
    QString bgColor = isDarkMode() ? "rgba(255, 255, 255, 0.05)" : "rgba(0, 0, 0, 0.03)";
    QString borderColor = isDarkMode() ? "rgba(255, 255, 255, 0.08)" : "rgba(0, 0, 0, 0.06)";

    return QString(
        "QGroupBox {"
        "    background-color: %1;"
        "    border: 1px solid %2;"
        "    border-radius: 8px;"
        "    margin-top: 8px;"
        "    padding: 8px 6px 6px 6px;"
        "    font-weight: 500;"
        "}"
        "QGroupBox::title {"
        "    subcontrol-origin: margin;"
        "    subcontrol-position: top left;"
        "    left: 10px;"
        "    padding: 0 4px;"
        "    color: %3;"
        "    font-size: 11px;"
        "    font-weight: 600;"
        "    text-transform: uppercase;"
        "    letter-spacing: 0.5px;"
        "}"
    ).arg(bgColor)
     .arg(borderColor)
     .arg(m_secondaryTextColor.name());
}

QString MacOSStyleManager::getPrimaryButtonStyle() const
{
    // macOS 26: Unified button style, mostly gray with blue for primary actions
    QString normalBg = isDarkMode() ? "rgba(255, 255, 255, 0.1)" : "rgba(0, 0, 0, 0.05)";
    QString hoverBg = isDarkMode() ? "rgba(255, 255, 255, 0.15)" : "rgba(0, 0, 0, 0.08)";
    QString pressedBg = isDarkMode() ? "rgba(255, 255, 255, 0.08)" : "rgba(0, 0, 0, 0.12)";

    return QString(
        "QPushButton {"
        "    background-color: %1;"
        "    color: %2;"
        "    border: none;"
        "    border-radius: 5px;"
        "    padding: 4px 12px;"
        "    font-size: 12px;"
        "    font-weight: 500;"
        "    min-height: 22px;"
        "}"
        "QPushButton:hover {"
        "    background-color: %3;"
        "}"
        "QPushButton:pressed {"
        "    background-color: %4;"
        "}"
        "QPushButton:disabled {"
        "    background-color: %5;"
        "    color: %6;"
        "}"
    ).arg(normalBg)
     .arg(m_textColor.name())
     .arg(hoverBg)
     .arg(pressedBg)
     .arg(isDarkMode() ? "rgba(255,255,255,0.05)" : "rgba(0,0,0,0.03)")
     .arg(m_secondaryTextColor.name());
}

QString MacOSStyleManager::getSecondaryButtonStyle() const
{
    return getPrimaryButtonStyle();  // Same as primary in macOS 26 style
}

QString MacOSStyleManager::getDangerButtonStyle() const
{
    // Even danger buttons are more subtle in macOS 26
    QString normalBg = isDarkMode() ? "rgba(255, 69, 58, 0.2)" : "rgba(255, 59, 48, 0.1)";
    QString hoverBg = isDarkMode() ? "rgba(255, 69, 58, 0.3)" : "rgba(255, 59, 48, 0.15)";
    QString pressedBg = isDarkMode() ? "rgba(255, 69, 58, 0.25)" : "rgba(255, 59, 48, 0.2)";

    return QString(
        "QPushButton {"
        "    background-color: %1;"
        "    color: %2;"
        "    border: none;"
        "    border-radius: 5px;"
        "    padding: 4px 12px;"
        "    font-size: 12px;"
        "    font-weight: 500;"
        "    min-height: 22px;"
        "}"
        "QPushButton:hover {"
        "    background-color: %3;"
        "}"
        "QPushButton:pressed {"
        "    background-color: %4;"
        "}"
    ).arg(normalBg)
     .arg(m_dangerColor.name())
     .arg(hoverBg)
     .arg(pressedBg);
}

QString MacOSStyleManager::getAccentButtonStyle() const
{
    // Blue accent button for primary actions
    return QString(
        "QPushButton {"
        "    background-color: %1;"
        "    color: white;"
        "    border: none;"
        "    border-radius: 5px;"
        "    padding: 4px 14px;"
        "    font-size: 12px;"
        "    font-weight: 600;"
        "    min-height: 22px;"
        "}"
        "QPushButton:hover {"
        "    background-color: %2;"
        "}"
        "QPushButton:pressed {"
        "    background-color: %3;"
        "}"
    ).arg(m_accentColor.name())
     .arg(m_accentColor.lighter(110).name())
     .arg(m_accentColor.darker(105).name());
}

QString MacOSStyleManager::getTextButtonStyle() const
{
    return QString(
        "QPushButton {"
        "    background-color: transparent;"
        "    color: %1;"
        "    border: none;"
        "    border-radius: 5px;"
        "    padding: 4px 12px;"
        "    font-size: 12px;"
        "    font-weight: 500;"
        "}"
        "QPushButton:hover {"
        "    background-color: %2;"
        "}"
    ).arg(m_accentColor.name())
     .arg(isDarkMode() ? "rgba(10, 132, 255, 0.15)" : "rgba(0, 122, 255, 0.1)");
}

QString MacOSStyleManager::getLineEditStyle() const
{
    QString bgColor = isDarkMode() ? "rgba(255, 255, 255, 0.07)" : "rgba(0, 0, 0, 0.04)";
    QString borderColor = isDarkMode() ? "rgba(255, 255, 255, 0.1)" : "rgba(0, 0, 0, 0.08)";
    QString focusBorder = m_accentColor.name();

    return QString(
        "QLineEdit {"
        "    background-color: %1;"
        "    color: %2;"
        "    border: 1px solid %3;"
        "    border-radius: 5px;"
        "    padding: 4px 8px;"
        "    font-size: 12px;"
        "    selection-background-color: %4;"
        "}"
        "QLineEdit:focus {"
        "    border: 1px solid %5;"
        "}"
        "QLineEdit:disabled {"
        "    color: %6;"
        "}"
        "QLineEdit[readOnly=\"true\"] {"
        "    background-color: %7;"
        "}"
    ).arg(bgColor)
     .arg(m_textColor.name())
     .arg(borderColor)
     .arg(colorToRgba(m_accentColor, 100))
     .arg(focusBorder)
     .arg(m_secondaryTextColor.name())
     .arg(isDarkMode() ? "rgba(255,255,255,0.04)" : "rgba(0,0,0,0.02)");
}

QString MacOSStyleManager::getSpinBoxStyle() const
{
    QString bgColor = isDarkMode() ? "rgba(255, 255, 255, 0.07)" : "rgba(0, 0, 0, 0.04)";
    QString borderColor = isDarkMode() ? "rgba(255, 255, 255, 0.1)" : "rgba(0, 0, 0, 0.08)";

    return QString(
        "QSpinBox {"
        "    background-color: %1;"
        "    color: %2;"
        "    border: 1px solid %3;"
        "    border-radius: 5px;"
        "    padding: 2px 6px;"
        "    font-size: 12px;"
        "    min-height: 20px;"
        "}"
        "QSpinBox:focus {"
        "    border: 1px solid %4;"
        "}"
        "QSpinBox::up-button, QSpinBox::down-button {"
        "    background-color: transparent;"
        "    border: none;"
        "    width: 14px;"
        "}"
        "QSpinBox::up-button:hover, QSpinBox::down-button:hover {"
        "    background-color: %5;"
        "}"
        "QSpinBox::up-arrow {"
        "    image: none;"
        "    border-left: 3px solid transparent;"
        "    border-right: 3px solid transparent;"
        "    border-bottom: 4px solid %6;"
        "    width: 0; height: 0;"
        "}"
        "QSpinBox::down-arrow {"
        "    image: none;"
        "    border-left: 3px solid transparent;"
        "    border-right: 3px solid transparent;"
        "    border-top: 4px solid %6;"
        "    width: 0; height: 0;"
        "}"
    ).arg(bgColor)
     .arg(m_textColor.name())
     .arg(borderColor)
     .arg(m_accentColor.name())
     .arg(isDarkMode() ? "rgba(255,255,255,0.1)" : "rgba(0,0,0,0.05)")
     .arg(m_secondaryTextColor.name());
}

QString MacOSStyleManager::getCheckBoxStyle() const
{
    QString uncheckedBg = isDarkMode() ? "rgba(255, 255, 255, 0.1)" : "rgba(0, 0, 0, 0.05)";
    QString border = isDarkMode() ? "rgba(255, 255, 255, 0.15)" : "rgba(0, 0, 0, 0.1)";

    return QString(
        "QCheckBox {"
        "    color: %1;"
        "    font-size: 12px;"
        "    spacing: 6px;"
        "}"
        "QCheckBox::indicator {"
        "    width: 16px;"
        "    height: 16px;"
        "    border-radius: 4px;"
        "    border: 1px solid %2;"
        "    background-color: %3;"
        "}"
        "QCheckBox::indicator:checked {"
        "    background-color: %4;"
        "    border-color: %4;"
        "}"
        "QCheckBox::indicator:hover {"
        "    border-color: %4;"
        "}"
    ).arg(m_textColor.name())
     .arg(border)
     .arg(uncheckedBg)
     .arg(m_accentColor.name());
}

QString MacOSStyleManager::getListWidgetStyle() const
{
    QString bgColor = isDarkMode() ? "rgba(255, 255, 255, 0.04)" : "rgba(0, 0, 0, 0.02)";
    QString borderColor = isDarkMode() ? "rgba(255, 255, 255, 0.08)" : "rgba(0, 0, 0, 0.05)";
    QString itemHover = isDarkMode() ? "rgba(255, 255, 255, 0.06)" : "rgba(0, 0, 0, 0.04)";
    QString itemSelected = isDarkMode() ? "rgba(10, 132, 255, 0.3)" : "rgba(0, 122, 255, 0.15)";

    return QString(
        "QListWidget {"
        "    background-color: %1;"
        "    color: %2;"
        "    border: 1px solid %3;"
        "    border-radius: 6px;"
        "    padding: 2px;"
        "    outline: none;"
        "}"
        "QListWidget::item {"
        "    padding: 5px 8px;"
        "    border-radius: 4px;"
        "    margin: 1px 2px;"
        "}"
        "QListWidget::item:selected {"
        "    background-color: %4;"
        "    color: %2;"
        "}"
        "QListWidget::item:hover:!selected {"
        "    background-color: %5;"
        "}"
    ).arg(bgColor)
     .arg(m_textColor.name())
     .arg(borderColor)
     .arg(itemSelected)
     .arg(itemHover);
}

QString MacOSStyleManager::getPlainTextEditStyle() const
{
    QString bgColor = isDarkMode() ? "rgba(0, 0, 0, 0.3)" : "rgba(255, 255, 255, 0.8)";
    QString borderColor = isDarkMode() ? "rgba(255, 255, 255, 0.08)" : "rgba(0, 0, 0, 0.05)";

    return QString(
        "QPlainTextEdit {"
        "    background-color: %1;"
        "    color: %2;"
        "    border: 1px solid %3;"
        "    border-radius: 6px;"
        "    padding: 6px;"
        "    font-family: 'SF Mono', Menlo, Monaco, monospace;"
        "    font-size: 11px;"
        "    selection-background-color: %4;"
        "}"
    ).arg(bgColor)
     .arg(m_textColor.name())
     .arg(borderColor)
     .arg(colorToRgba(m_accentColor, 80));
}

QString MacOSStyleManager::getLabelStyle() const
{
    return QString(
        "QLabel {"
        "    color: %1;"
        "    font-size: 12px;"
        "    background: transparent;"
        "}"
    ).arg(m_textColor.name());
}

QString MacOSStyleManager::getSplitterStyle() const
{
    return QString(
        "QSplitter::handle {"
        "    background-color: transparent;"
        "}"
        "QSplitter::handle:vertical {"
        "    height: 4px;"
        "}"
        "QSplitter::handle:horizontal {"
        "    width: 4px;"
        "}"
    );
}

QString MacOSStyleManager::getMenuBarStyle() const
{
    return QString(
        "QMenuBar {"
        "    background-color: transparent;"
        "    color: %1;"
        "    font-size: 13px;"
        "}"
        "QMenuBar::item {"
        "    padding: 4px 8px;"
        "    border-radius: 4px;"
        "    background-color: transparent;"
        "}"
        "QMenuBar::item:selected {"
        "    background-color: %2;"
        "}"
        "QMenu {"
        "    background-color: %3;"
        "    border: 1px solid %4;"
        "    border-radius: 8px;"
        "    padding: 4px;"
        "}"
        "QMenu::item {"
        "    padding: 5px 20px 5px 10px;"
        "    border-radius: 4px;"
        "    color: %1;"
        "}"
        "QMenu::item:selected {"
        "    background-color: %5;"
        "}"
        "QMenu::separator {"
        "    height: 1px;"
        "    background-color: %6;"
        "    margin: 4px 8px;"
        "}"
    ).arg(m_textColor.name())
     .arg(isDarkMode() ? "rgba(255,255,255,0.1)" : "rgba(0,0,0,0.05)")
     .arg(m_secondaryBackgroundColor.name())
     .arg(m_borderColor.name())
     .arg(isDarkMode() ? "rgba(10,132,255,0.3)" : "rgba(0,122,255,0.15)")
     .arg(m_separatorColor.name());
}

QString MacOSStyleManager::getStatusBarStyle() const
{
    return QString(
        "QStatusBar {"
        "    background-color: transparent;"
        "    color: %1;"
        "    font-size: 11px;"
        "}"
        "QStatusBar::item {"
        "    border: none;"
        "}"
    ).arg(m_secondaryTextColor.name());
}

QString MacOSStyleManager::getScrollBarStyle() const
{
    QString handleColor = isDarkMode() ? "rgba(255,255,255,0.25)" : "rgba(0,0,0,0.2)";
    QString handleHoverColor = isDarkMode() ? "rgba(255,255,255,0.4)" : "rgba(0,0,0,0.35)";

    return QString(
        "QScrollBar:vertical {"
        "    background: transparent;"
        "    width: 10px;"
        "    margin: 2px;"
        "}"
        "QScrollBar::handle:vertical {"
        "    background: %1;"
        "    border-radius: 3px;"
        "    min-height: 24px;"
        "}"
        "QScrollBar::handle:vertical:hover {"
        "    background: %2;"
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
        "    height: 0px;"
        "}"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {"
        "    background: transparent;"
        "}"
        "QScrollBar:horizontal {"
        "    background: transparent;"
        "    height: 10px;"
        "    margin: 2px;"
        "}"
        "QScrollBar::handle:horizontal {"
        "    background: %1;"
        "    border-radius: 3px;"
        "    min-width: 24px;"
        "}"
        "QScrollBar::handle:horizontal:hover {"
        "    background: %2;"
        "}"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {"
        "    width: 0px;"
        "}"
        "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {"
        "    background: transparent;"
        "}"
    ).arg(handleColor)
     .arg(handleHoverColor);
}

QString MacOSStyleManager::getToolTipStyle() const
{
    return QString(
        "QToolTip {"
        "    background-color: %1;"
        "    color: %2;"
        "    border: 1px solid %3;"
        "    border-radius: 4px;"
        "    padding: 4px 8px;"
        "    font-size: 11px;"
        "}"
    ).arg(isDarkMode() ? "#3a3a3c" : "#f5f5f7")
     .arg(m_textColor.name())
     .arg(m_borderColor.name());
}

// Color getters
QColor MacOSStyleManager::backgroundColor() const { return m_backgroundColor; }
QColor MacOSStyleManager::secondaryBackgroundColor() const { return m_secondaryBackgroundColor; }
QColor MacOSStyleManager::tertiaryBackgroundColor() const { return m_tertiaryBackgroundColor; }
QColor MacOSStyleManager::textColor() const { return m_textColor; }
QColor MacOSStyleManager::secondaryTextColor() const { return m_secondaryTextColor; }
QColor MacOSStyleManager::accentColor() const { return m_accentColor; }
QColor MacOSStyleManager::borderColor() const { return m_borderColor; }
QColor MacOSStyleManager::separatorColor() const { return m_separatorColor; }
QColor MacOSStyleManager::successColor() const { return m_successColor; }
QColor MacOSStyleManager::dangerColor() const { return m_dangerColor; }
QColor MacOSStyleManager::warningColor() const { return m_warningColor; }
