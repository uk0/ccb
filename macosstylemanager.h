#ifndef MACOSSTYLEMANAGER_H
#define MACOSSTYLEMANAGER_H

#include <QObject>
#include <QWidget>
#include <QString>
#include <QColor>

class MacOSStyleManager : public QObject
{
    Q_OBJECT

public:
    enum Theme {
        Light,
        Dark,
        System  // Auto-detect from system
    };

    static MacOSStyleManager& instance();

    // Theme detection and management
    void initialize(QWidget *mainWindow);
    Theme currentTheme() const;
    bool isDarkMode() const;

    // Apply styles to widgets
    void applyToWidget(QWidget *widget);
    void applyToMainWindow(QWidget *mainWindow);
    void applyToDialog(QWidget *dialog);

    // Get style strings
    QString getFullStyleSheet() const;
    QString getMainWindowStyle() const;
    QString getDialogStyle() const;
    QString getGroupBoxStyle() const;
    QString getPrimaryButtonStyle() const;
    QString getSecondaryButtonStyle() const;
    QString getDangerButtonStyle() const;
    QString getAccentButtonStyle() const;
    QString getTextButtonStyle() const;
    QString getLineEditStyle() const;
    QString getSpinBoxStyle() const;
    QString getCheckBoxStyle() const;
    QString getListWidgetStyle() const;
    QString getPlainTextEditStyle() const;
    QString getLabelStyle() const;
    QString getSplitterStyle() const;
    QString getMenuBarStyle() const;
    QString getStatusBarStyle() const;
    QString getScrollBarStyle() const;
    QString getToolTipStyle() const;

    // Colors
    QColor backgroundColor() const;
    QColor secondaryBackgroundColor() const;
    QColor tertiaryBackgroundColor() const;
    QColor textColor() const;
    QColor secondaryTextColor() const;
    QColor accentColor() const;
    QColor borderColor() const;
    QColor separatorColor() const;
    QColor successColor() const;
    QColor dangerColor() const;
    QColor warningColor() const;

signals:
    void themeChanged(Theme newTheme);

public slots:
    void setTheme(Theme theme);
    void refreshTheme();

private:
    explicit MacOSStyleManager(QObject *parent = nullptr);
    ~MacOSStyleManager() = default;
    MacOSStyleManager(const MacOSStyleManager&) = delete;
    MacOSStyleManager& operator=(const MacOSStyleManager&) = delete;

    Theme detectSystemTheme() const;
    void updateColors();
    QString colorToRgba(const QColor &color, int alpha = 255) const;

    Theme m_theme = System;
    Theme m_effectiveTheme = Light;
    QWidget *m_mainWindow = nullptr;

    // Cached colors
    QColor m_backgroundColor;
    QColor m_secondaryBackgroundColor;
    QColor m_tertiaryBackgroundColor;
    QColor m_textColor;
    QColor m_secondaryTextColor;
    QColor m_accentColor;
    QColor m_borderColor;
    QColor m_separatorColor;
    QColor m_successColor;
    QColor m_dangerColor;
    QColor m_warningColor;
};

#endif // MACOSSTYLEMANAGER_H
