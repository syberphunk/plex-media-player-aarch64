#ifndef UPDATEMANAGER_H
#define UPDATEMANAGER_H

#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include "ComponentManager.h"
#include "utils/Utils.h"

class QNetworkAccessManager;
class QNetworkReply;

class UpdateManager : public ComponentBase
{
  Q_OBJECT
  DEFINE_SINGLETON(UpdateManager);

public:
  enum State
  {
    StateIdle = 0,
    StateAvailable = 1,
    StateUpdating = 2,
    StateDone = 3,
    StateManual = 4,
    StateFailed = 5
  };
  Q_ENUM(State)

  const char* componentName() override { return "updater"; }
  bool componentExport() override { return false; }
  bool componentInitialize() override;
  void componentPostInitialize() override;

  Q_PROPERTY(int state READ state NOTIFY stateChanged)
  Q_PROPERTY(bool checkEnabled READ checkEnabled NOTIFY checkEnabledChanged)
  Q_PROPERTY(bool needsPermission READ needsPermission NOTIFY stateChanged)
  Q_PROPERTY(bool infoVisible READ infoVisible NOTIFY infoVisibleChanged)
  Q_PROPERTY(bool updatePending READ updatePending NOTIFY availableVersionChanged)
  Q_PROPERTY(QString manualCommand READ manualCommand CONSTANT)
  Q_PROPERTY(QString permissionCommand READ permissionCommand CONSTANT)
  Q_PROPERTY(QString installedVersion READ installedVersion CONSTANT)
  Q_PROPERTY(QString availableVersion READ availableVersion NOTIFY availableVersionChanged)
  Q_PROPERTY(QString releaseNotes READ releaseNotes NOTIFY releaseNotesChanged)
  Q_PROPERTY(QVariantList releaseNoteBlocks READ releaseNoteBlocks NOTIFY releaseNotesChanged)
  Q_PROPERTY(int notesStatus READ notesStatus NOTIFY releaseNotesChanged)
  Q_PROPERTY(int opIndex READ opIndex NOTIFY progressChanged)
  Q_PROPERTY(int opCount READ opCount NOTIFY progressChanged)
  Q_PROPERTY(int opProgress READ opProgress NOTIFY progressChanged)
  Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged)

  enum NotesStatus
  {
    NotesIdle = 0,
    NotesLoading = 1,
    NotesReady = 2,
    NotesFailed = 3
  };
  Q_ENUM(NotesStatus)

  int state() const { return m_state; }
  bool checkEnabled() const;
  bool needsPermission() const { return m_needsPermission; }
  bool infoVisible() const { return m_infoVisible; }
  bool updatePending() const { return !m_remoteCommit.isEmpty() && m_remoteCommit != m_localCommit; }
  QString manualCommand() const;
  QString permissionCommand() const;
  QString installedVersion() const;
  QString availableVersion() const { return m_availableVersion; }
  QString releaseNotes() const { return m_releaseNotes; }
  QVariantList releaseNoteBlocks() const { return m_releaseNoteBlocks; }
  int notesStatus() const { return m_notesStatus; }
  int opIndex() const { return m_opIndex; }
  int opCount() const { return m_opCount; }
  int opProgress() const { return m_opProgress; }
  QString errorMessage() const { return m_errorMessage; }

  Q_INVOKABLE void fetchReleaseNotes();
  Q_INVOKABLE void showInfo();
  Q_INVOKABLE void hideInfo();
  Q_INVOKABLE void toggleInfo();
  Q_INVOKABLE void showManual();
  Q_INVOKABLE void applyUpdate();
  Q_INVOKABLE void dismiss();
  Q_INVOKABLE void skipVersion();
  Q_INVOKABLE void disableChecks();
  Q_INVOKABLE void setCheckEnabled(bool enabled);
  Q_INVOKABLE void restartApp();

  Q_SIGNAL void stateChanged();
  Q_SIGNAL void infoVisibleChanged();
  Q_SIGNAL void checkEnabledChanged();
  Q_SIGNAL void availableVersionChanged();
  Q_SIGNAL void releaseNotesChanged();
  Q_SIGNAL void progressChanged();

private:
  explicit UpdateManager(QObject* parent = nullptr);

  void readSandboxInfo();
  bool startMonitor();
  void stopMonitor();
  void setState(State state);
  void applyFailed(const QString& name, const QString& message);
  void setNotesStatus(NotesStatus status);

  Q_SLOT void onSettingsChanged(const QString& section, const QVariantMap& values);
  Q_SLOT void onUpdateAvailable(const QVariantMap& info);
  Q_SLOT void onProgress(const QVariantMap& info);
  Q_SLOT void onNotesFinished();

  State m_state = StateIdle;
  NotesStatus m_notesStatus = NotesIdle;

  bool m_inSandbox = false;
  bool m_needsPermission = false;
  bool m_infoVisible = false;
  QString m_appId;
  QString m_monitorPath;

  QString m_runningCommit;
  QString m_localCommit;
  QString m_remoteCommit;
  QString m_availableVersion;
  QString m_releaseNotes;
  QVariantList m_releaseNoteBlocks;
  QString m_errorMessage;

  int m_opIndex = 0;
  int m_opCount = 0;
  int m_opProgress = 0;

  QNetworkAccessManager* m_network = nullptr;
  QNetworkReply* m_notesReply = nullptr;
};

#endif // UPDATEMANAGER_H
