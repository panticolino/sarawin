#ifndef SARA_UI_SETTINGS_DIALOG_H
#define SARA_UI_SETTINGS_DIALOG_H

#include "core/types.h"
#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QStackedWidget>
#include <QListWidget>

namespace sara {

class BackupManager;
class UserManager;

/**
 * Diálogo de configuración con dos modos:
 *
 * 1. Wizard (primera ejecución): pasos secuenciales con Anterior/Siguiente
 * 2. Normal (pestañas laterales): acceso directo a cada sección
 *
 * Secciones:
 * - Perfil de la Radio
 * - Modo Automático (carpeta, crossfade, anti-repetición)
 * - Tarjetas de Audio (Main, CUE, InstantPlay)
 * - Locuciones de Hora
 * - Espacio Publicitario
 */
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(AppConfig& config, bool wizardMode = false,
                           QWidget* parent = nullptr);

    AppConfig result() const { return config_; }

private slots:
    void nextStep();
    void prevStep();
    void onSave();
    void browseFallback();
    void browseHoursFolder();
    void browseMinutesFolder();

private:
    void setupWizard();
    void setupNormal();

    QWidget* createPageProfile();
    QWidget* createPageInterface();
    QWidget* createPageAuto();
    QWidget* createPageAudioDevices();
    QWidget* createPageAudioConfig();
    QWidget* createPageTimeAnnounce();
    QWidget* createPageAdBreak();
    QWidget* createPagePisadores();
    QWidget* createPageBackup();
    QWidget* createPageUsers();

    void refreshBackupList();
    void refreshUserList();

    void collectConfig();
    void updateStepIndicator();

    AppConfig config_;
    bool wizardMode_;
    int currentStep_ = 0;
    int totalSteps_ = 5;

    // Layout
    QStackedWidget* stack_;
    QListWidget*    sidebar_;       // Solo en modo normal
    QLabel*         stepLabel_;     // Solo en modo wizard
    QPushButton*    prevButton_;    // Solo en modo wizard
    QPushButton*    nextButton_;    // Solo en modo wizard

    // Campos — Perfil
    QLineEdit* radioNameEdit_;
    QLineEdit* radioSloganEdit_;
    QLineEdit* radioFrequencyEdit_;
    QLineEdit* radioCityEdit_;
    QLineEdit* radioCountryEdit_;
    QSpinBox*  defaultVolumeSpin_ = nullptr;
    QSpinBox*  talkoverLevelSpin_ = nullptr;

    // Campos — Interfaz
    QComboBox* languageCombo_ = nullptr;
    QComboBox* themeCombo_    = nullptr;
    QComboBox* fontSizeCombo_ = nullptr;

    // Campos — Modo Automático
    QLineEdit* radioFolderEdit_ = nullptr;
    QLineEdit* fallbackFolderEdit_;
    QCheckBox* crossfadeCheck_ = nullptr;
    QSpinBox*  crossfadeSpin_ = nullptr;
    QCheckBox* fadeOutCheck_ = nullptr;
    QSpinBox*  fadeOutSpin_ = nullptr;
    QComboBox* startupModeCombo_;
    QLineEdit* recFolderEdit_ = nullptr;
    QComboBox* recFormatCombo_ = nullptr;
    QSpinBox*  recBitrateSpin_ = nullptr;
    QCheckBox* recSegmentCheck_ = nullptr;

    // Streaming / Butt
    QCheckBox* nowPlayingCheck_ = nullptr;
    QLineEdit* nowPlayingFileEdit_ = nullptr;
    QSpinBox*  noRepeatSpin_ = nullptr;
    QSpinBox*  noRepeatArtistSpin_ = nullptr;
    QCheckBox* silenceCheck_ = nullptr;
    QSpinBox*  silenceThresholdSpin_ = nullptr;
    QSpinBox*  silenceLevelSpin_ = nullptr;
    QCheckBox* replayGainCheck_ = nullptr;

    // Campos — Tarjetas de Audio
    QComboBox* audioDeviceCombo_;
    QComboBox* cueDeviceCombo_;
    QComboBox* instantDeviceCombo_;
    QComboBox* recordDeviceCombo_ = nullptr;

    // Campos — Locuciones de Hora
    QLineEdit* hoursFolderEdit_;
    QLineEdit* minutesFolderEdit_;
    QLineEdit* prefixFileEdit_;
    QLineEdit* suffixFileEdit_;

    // Campos — Espacio Publicitario
    QLineEdit* adIntroFileEdit_;
    QLineEdit* adOutroFileEdit_;

    // Campos — Pisadores
    QCheckBox* pisadorEnabledCheck_ = nullptr;
    QLineEdit* pisadorFolderEdit_ = nullptr;
    QComboBox* pisadorFreqCombo_ = nullptr;
    QSpinBox*  pisadorDuckSpin_ = nullptr;
    QSpinBox*  pisadorDelaySpin_ = nullptr;
    QListWidget* pisadorExcludedList_ = nullptr;

    // Campos — VU Meter
    QCheckBox* vuMeterCheck_ = nullptr;

    // Campos — Backup
    QCheckBox* backupEnabledCheck_ = nullptr;
    QComboBox* backupIntervalCombo_ = nullptr;
    QComboBox* backupMaxCombo_ = nullptr;
    QListWidget* backupListWidget_ = nullptr;
    QListWidget* userListWidget_ = nullptr;

    // Dependencias externas para backup
    BackupManager* backupManager_ = nullptr;
    UserManager*   userManager_ = nullptr;
    UserRole       userRole_ = UserRole::Admin;

public:
    void setBackupManager(BackupManager* mgr) { backupManager_ = mgr; refreshBackupList(); }
    void setUserManager(UserManager* mgr) { userManager_ = mgr; refreshUserList(); }
    void setUserRole(UserRole role);
};

} // namespace sara
#endif
