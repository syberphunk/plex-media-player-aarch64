import QtQuick

FocusScope
{
  id: overlay

  property var updater: (typeof components !== "undefined") ? components.updater : null
  property var cfg: (typeof components !== "undefined") ? components.menu : null

  readonly property int stateIdle: 0
  readonly property int stateAvailable: 1
  readonly property int stateUpdating: 2
  readonly property int stateDone: 3
  readonly property int stateManual: 4
  readonly property int stateFailed: 5

  readonly property int notesIdle: 0
  readonly property int notesLoading: 1
  readonly property int notesReady: 2
  readonly property int notesFailed: 3

  readonly property int viewBanner: 0
  readonly property int viewNotes: 1
  readonly property int viewConfirm: 2

  property int view: viewBanner

  readonly property int updateState: updater ? updater.state : stateIdle

  property real u: Math.max(0.6, Math.min(2.0, overlay.height / 1080))
  property real inset: Math.round(overlay.height * 0.035)

  readonly property string surface:    "#181A1F"
  readonly property string panel:      "#22252B"
  readonly property string textPri:    "#ECEEF1"
  readonly property string textSec:    "#B9BFC8"
  readonly property string accentText: "#E79A3C"
  readonly property string accentFill: "#E08B20"
  readonly property string onAccent:   "#101216"
  readonly property string outline:    "#767E8A"
  readonly property string ringOuter:  "#F2F4F7"
  readonly property string ringInner:  "#0B0D10"
  readonly property string track:      "#2A2E35"
  readonly property string rule:       "#2F333A"

  readonly property bool infoOpen: updater ? updater.infoVisible : false
  readonly property bool haveUpdate: updateState === stateAvailable

  readonly property bool showBanner: !infoOpen && haveUpdate && view === viewBanner
  readonly property bool showNotes: (infoOpen && view !== viewConfirm)
                                    || (haveUpdate && view === viewNotes)
  readonly property bool showConfirm: (infoOpen || haveUpdate) && view === viewConfirm
  readonly property bool showProgress: !infoOpen && updateState === stateUpdating
  readonly property bool showDone: !infoOpen && updateState === stateDone
  readonly property bool showManual: !infoOpen
                                     && (updateState === stateManual || updateState === stateFailed)

  readonly property bool modal: showNotes || showConfirm || showProgress || showDone || showManual

  signal closed()

  visible: opacity > 0
  opacity: (updateState === stateIdle && !infoOpen) ? 0 : 1

  Behavior on opacity
  {
    NumberAnimation { duration: 150 }
  }

  onModalChanged:
  {
    if (cfg)
      cfg.grabKeyboard("update", modal)
  }

  onUpdateStateChanged:
  {
    view = viewBanner

    if (updateState === stateIdle && !infoOpen)
    {
      overlay.closed()
      return;
    }

    Qt.callLater(overlay.takeFocus)
  }

  onInfoOpenChanged:
  {
    if (!infoOpen && updateState === stateIdle)
    {
      overlay.closed()
      return;
    }

    Qt.callLater(overlay.takeFocus)
  }

  function takeFocus()
  {
    if (showBanner)
      bannerOpen.forceActiveFocus()
    else if (showNotes)
      notesApply.visible ? notesApply.forceActiveFocus() : notesClose.forceActiveFocus()
    else if (showConfirm)
      confirmNo.forceActiveFocus()
    else if (showDone)
      doneRestart.forceActiveFocus()
    else if (showManual)
      manualClose.forceActiveFocus()
    else
      overlay.forceActiveFocus()
  }

  function openNotes()
  {
    if (updater && updater.notesStatus === notesIdle)
      updater.fetchReleaseNotes()

    view = viewNotes
    Qt.callLater(overlay.takeFocus)
  }

  function close()
  {
    if (showConfirm)
    {
      view = viewNotes
      Qt.callLater(overlay.takeFocus)
      return;
    }

    if (infoOpen)
    {
      updater.hideInfo()
      return;
    }

    if (showNotes)
    {
      view = viewBanner
      Qt.callLater(overlay.takeFocus)
      return;
    }

    if (!updater)
      return;

    if (showManual || showDone)
      updater.dismiss()
    else if (showBanner)
      updater.dismiss()
  }

  Keys.onEscapePressed: overlay.close()
  Keys.onBackPressed: overlay.close()

  component Btn : Item
  {
    id: btn

    property string label: ""
    property string hint: ""
    property bool primary: false
    property real minW: 240

    signal clicked()

    implicitWidth: Math.max(Math.round(minW * overlay.u),
                            btnLabel.implicitWidth + Math.round(56 * overlay.u))
    implicitHeight: Math.round(64 * overlay.u)
    activeFocusOnTab: true

    Accessible.role: Accessible.Button
    Accessible.name: btn.label
    Accessible.description: btn.hint
    Accessible.onPressAction: btn.clicked()

    Keys.onReturnPressed: btn.clicked()
    Keys.onEnterPressed: btn.clicked()
    Keys.onSpacePressed: btn.clicked()

    Rectangle
    {
      anchors.fill: parent
      radius: Math.round(8 * overlay.u)
      color: btn.primary ? overlay.accentFill : "transparent"
      border.width: btn.primary ? 0 : Math.max(1, Math.round(2 * overlay.u))
      border.color: overlay.outline

      Text
      {
        id: btnLabel
        anchors.centerIn: parent
        text: btn.label
        color: btn.primary ? overlay.onAccent : overlay.textPri
        font.pixelSize: Math.round(30 * overlay.u)
        font.weight: Font.DemiBold
      }
    }

    Rectangle
    {
      visible: btn.activeFocus
      anchors.fill: parent
      anchors.margins: -Math.round(8 * overlay.u)
      radius: Math.round(16 * overlay.u)
      color: "transparent"
      border.width: Math.round(4 * overlay.u)
      border.color: overlay.ringOuter
    }

    Rectangle
    {
      visible: btn.activeFocus
      anchors.fill: parent
      anchors.margins: -Math.round(3 * overlay.u)
      radius: Math.round(11 * overlay.u)
      color: "transparent"
      border.width: Math.round(3 * overlay.u)
      border.color: overlay.ringInner
    }

    MouseArea
    {
      anchors.fill: parent
      cursorShape: Qt.PointingHandCursor
      onClicked:
      {
        btn.forceActiveFocus()
        btn.clicked()
      }
    }
  }

  component Card : Rectangle
  {
    radius: Math.round(14 * overlay.u)
    color: overlay.surface
    border.width: Math.max(1, Math.round(2 * overlay.u))
    border.color: overlay.outline
  }

  Rectangle
  {
    anchors.fill: parent
    color: "black"
    opacity: 0.72
    visible: overlay.modal

    MouseArea
    {
      anchors.fill: parent
    }
  }

  Card
  {
    id: banner
    visible: overlay.showBanner
    width: Math.round(620 * overlay.u)
    height: Math.round(232 * overlay.u)
    anchors.right: parent.right
    anchors.bottom: parent.bottom
    anchors.rightMargin: overlay.inset
    anchors.bottomMargin: overlay.inset

    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Update available")

    Text
    {
      id: bannerTitle
      x: Math.round(32 * overlay.u)
      y: Math.round(26 * overlay.u)
      text: qsTr("Update available")
      color: overlay.textPri
      font.pixelSize: Math.round(30 * overlay.u)
      font.weight: Font.DemiBold
    }

    Text
    {
      anchors.left: bannerTitle.left
      anchors.top: bannerTitle.bottom
      anchors.topMargin: Math.round(4 * overlay.u)
      width: banner.width - Math.round(120 * overlay.u)
      elide: Text.ElideRight
      textFormat: Text.PlainText
      text: overlay.updater ? overlay.updater.availableVersion : ""
      color: overlay.accentText
      font.pixelSize: Math.round(24 * overlay.u)
    }

    Item
    {
      id: bannerDismiss
      width: Math.round(48 * overlay.u)
      height: width
      anchors.right: parent.right
      anchors.top: parent.top
      anchors.margins: Math.round(24 * overlay.u)
      activeFocusOnTab: true

      Accessible.role: Accessible.Button
      Accessible.name: qsTr("Close")
      Accessible.description: qsTr("Hide this until the next time Plex starts")
      Accessible.onPressAction: overlay.close()

      Keys.onReturnPressed: overlay.close()
      Keys.onEnterPressed: overlay.close()
      Keys.onSpacePressed: overlay.close()

      Rectangle
      {
        anchors.fill: parent
        radius: Math.round(6 * overlay.u)
        color: "transparent"
        border.width: Math.max(1, Math.round(2 * overlay.u))
        border.color: overlay.outline

        Text
        {
          anchors.centerIn: parent
          text: "✕"
          color: overlay.textSec
          font.pixelSize: Math.round(24 * overlay.u)
        }
      }

      Rectangle
      {
        visible: bannerDismiss.activeFocus
        anchors.fill: parent
        anchors.margins: -Math.round(8 * overlay.u)
        radius: Math.round(14 * overlay.u)
        color: "transparent"
        border.width: Math.round(4 * overlay.u)
        border.color: overlay.ringOuter
      }

      Rectangle
      {
        visible: bannerDismiss.activeFocus
        anchors.fill: parent
        anchors.margins: -Math.round(3 * overlay.u)
        radius: Math.round(9 * overlay.u)
        color: "transparent"
        border.width: Math.round(3 * overlay.u)
        border.color: overlay.ringInner
      }

      MouseArea
      {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked:
        {
          bannerDismiss.forceActiveFocus()
          overlay.close()
        }
      }
    }

    Btn
    {
      id: bannerOpen
      label: qsTr("Press here to see what changed")
      hint: qsTr("Opens the release notes for this update")
      minW: 520
      anchors.left: parent.left
      anchors.right: parent.right
      anchors.bottom: parent.bottom
      anchors.leftMargin: Math.round(32 * overlay.u)
      anchors.rightMargin: Math.round(32 * overlay.u)
      anchors.bottomMargin: Math.round(30 * overlay.u)
      KeyNavigation.tab: bannerDismiss
      KeyNavigation.up: bannerDismiss
      onClicked: overlay.openNotes()
    }
  }

  Card
  {
    id: dialog
    visible: overlay.showNotes
    width: Math.min(Math.round(1180 * overlay.u), overlay.width * 0.8)
    height: Math.round(overlay.height * 0.74)
    anchors.centerIn: parent

    property real pad: Math.round(48 * overlay.u)

    Accessible.role: Accessible.Dialog
    Accessible.name: dialogTitle.text

    Text
    {
      id: dialogTitle
      x: dialog.pad
      y: dialog.pad
      text: overlay.haveUpdate ? qsTr("Update available") : qsTr("Release notes")
      color: overlay.textPri
      font.pixelSize: Math.round(44 * overlay.u)
      font.weight: Font.Bold
    }

    Text
    {
      id: dialogMeta
      x: dialog.pad
      width: dialog.width - dialog.pad * 2
      anchors.top: dialogTitle.bottom
      anchors.topMargin: Math.round(10 * overlay.u)
      elide: Text.ElideRight
      textFormat: Text.PlainText
      text:
      {
        if (!overlay.updater)
          return ""
        if (overlay.haveUpdate)
          return qsTr("installed %1   →   available %2").arg(overlay.updater.installedVersion)
                                                        .arg(overlay.updater.availableVersion)
        if (overlay.updater.availableVersion.length > 0)
          return qsTr("installed %1   ·   latest release %2")
                 .arg(overlay.updater.installedVersion).arg(overlay.updater.availableVersion)
        return qsTr("installed %1").arg(overlay.updater.installedVersion)
      }
      color: overlay.textSec
      font.pixelSize: Math.round(24 * overlay.u)
    }

    Rectangle
    {
      id: dialogRule
      x: dialog.pad
      width: dialog.width - dialog.pad * 2
      height: Math.max(1, Math.round(2 * overlay.u))
      color: overlay.rule
      anchors.top: dialogMeta.bottom
      anchors.topMargin: Math.round(26 * overlay.u)
    }

    Flickable
    {
      id: notes
      x: dialog.pad
      anchors.top: dialogRule.bottom
      anchors.topMargin: Math.round(26 * overlay.u)
      width: Math.min(Math.round(920 * overlay.u),
                      dialog.width - dialog.pad * 2 - Math.round(40 * overlay.u))
      height: dialog.height - dialog.pad * 2 - dialogTitle.height - dialogMeta.height
              - Math.round(200 * overlay.u)
      clip: true
      activeFocusOnTab: true
      contentWidth: width
      contentHeight: notesBody.implicitHeight
      boundsBehavior: Flickable.StopAtBounds
      flickDeceleration: 4000

      Accessible.role: Accessible.StaticText
      Accessible.name: qsTr("Release notes")
      Accessible.description: overlay.updater ? overlay.updater.releaseNotes : ""

      Keys.onDownPressed: notes.flick(0, -1200)
      Keys.onUpPressed: notes.flick(0, 1200)
      Keys.onPressed: (event) =>
      {
        if (event.key === Qt.Key_PageDown)
        {
          notes.contentY = Math.min(notes.contentY + notes.height,
                                    Math.max(0, notes.contentHeight - notes.height))
          event.accepted = true
        }
        else if (event.key === Qt.Key_PageUp)
        {
          notes.contentY = Math.max(0, notes.contentY - notes.height)
          event.accepted = true
        }
      }

      Column
      {
        id: notesBody
        width: notes.width
        spacing: Math.round(18 * overlay.u)

        Text
        {
          width: notesBody.width
          visible: !overlay.updater || overlay.updater.notesStatus !== overlay.notesReady
          wrapMode: Text.WordWrap
          textFormat: Text.PlainText
          color: overlay.textSec
          font.pixelSize: Math.round(28 * overlay.u)
          text:
          {
            if (!overlay.updater)
              return ""
            if (overlay.updater.notesStatus === overlay.notesLoading)
              return qsTr("Fetching the release notes…")
            if (overlay.updater.notesStatus === overlay.notesFailed)
              return qsTr("The release notes could not be fetched.")
            return ""
          }
        }

        Repeater
        {
          model: overlay.updater && overlay.updater.notesStatus === overlay.notesReady
                 ? overlay.updater.releaseNoteBlocks : []

          delegate: Text
          {
            required property var modelData

            width: notesBody.width
            topPadding: modelData.heading ? Math.round(14 * overlay.u) : 0
            wrapMode: Text.WordWrap
            textFormat: Text.PlainText
            lineHeight: 1.3
            text: modelData.text
            color: modelData.heading ? overlay.accentText : overlay.textPri
            font.pixelSize: Math.round((modelData.heading ? 32 : 28) * overlay.u)
            font.weight: modelData.heading ? Font.DemiBold : Font.Normal
          }
        }
      }
    }

    Rectangle
    {
      visible: notes.contentHeight > notes.height
      width: Math.round(10 * overlay.u)
      radius: width / 2
      color: overlay.track
      x: dialog.width - dialog.pad - width
      y: notes.y
      height: notes.height

      Rectangle
      {
        width: parent.width
        radius: width / 2
        color: notes.activeFocus ? overlay.accentFill : overlay.outline
        height: Math.max(Math.round(40 * overlay.u),
                         parent.height * (notes.height / Math.max(1, notes.contentHeight)))
        y: (parent.height - height)
           * (notes.contentY / Math.max(1, notes.contentHeight - notes.height))
      }
    }

    Item
    {
      id: checkToggle
      x: dialog.pad
      anchors.bottom: notesButtons.top
      anchors.bottomMargin: Math.round(28 * overlay.u)
      width: toggleLabel.x + toggleLabel.implicitWidth
      height: Math.round(40 * overlay.u)
      activeFocusOnTab: true

      readonly property bool on: overlay.updater ? overlay.updater.checkEnabled : false

      function toggle()
      {
        if (overlay.updater)
          overlay.updater.setCheckEnabled(!checkToggle.on)
      }

      Accessible.role: Accessible.CheckBox
      Accessible.name: qsTr("Check for updates")
      Accessible.checkable: true
      Accessible.checked: checkToggle.on
      Accessible.onToggleAction: checkToggle.toggle()
      Accessible.onPressAction: checkToggle.toggle()

      Keys.onReturnPressed: checkToggle.toggle()
      Keys.onEnterPressed: checkToggle.toggle()
      Keys.onSpacePressed: checkToggle.toggle()

      Rectangle
      {
        id: toggleBox
        width: Math.round(34 * overlay.u)
        height: width
        anchors.verticalCenter: parent.verticalCenter
        radius: Math.round(5 * overlay.u)
        color: "transparent"
        border.width: Math.max(1, Math.round(2 * overlay.u))
        border.color: overlay.outline

        Text
        {
          anchors.centerIn: parent
          visible: checkToggle.on
          text: "✓"
          color: overlay.accentText
          font.pixelSize: Math.round(26 * overlay.u)
          font.weight: Font.Bold
        }
      }

      Text
      {
        id: toggleLabel
        x: toggleBox.width + Math.round(16 * overlay.u)
        anchors.verticalCenter: parent.verticalCenter
        text: qsTr("Check for updates")
        color: overlay.textPri
        font.pixelSize: Math.round(26 * overlay.u)
      }

      Rectangle
      {
        visible: checkToggle.activeFocus
        anchors.fill: parent
        anchors.margins: -Math.round(8 * overlay.u)
        radius: Math.round(10 * overlay.u)
        color: "transparent"
        border.width: Math.round(4 * overlay.u)
        border.color: overlay.ringOuter
      }

      Rectangle
      {
        visible: checkToggle.activeFocus
        anchors.fill: parent
        anchors.margins: -Math.round(3 * overlay.u)
        radius: Math.round(6 * overlay.u)
        color: "transparent"
        border.width: Math.round(3 * overlay.u)
        border.color: overlay.ringInner
      }

      MouseArea
      {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked:
        {
          checkToggle.forceActiveFocus()
          checkToggle.toggle()
        }
      }
    }

    Row
    {
      id: notesButtons
      spacing: Math.round(24 * overlay.u)
      x: dialog.pad
      anchors.bottom: parent.bottom
      anchors.bottomMargin: dialog.pad

      Btn
      {
        id: notesApply
        label: qsTr("Update now")
        hint: qsTr("Installs the update now")
        visible: overlay.haveUpdate
        primary: true
        KeyNavigation.right: notesClose
        KeyNavigation.tab: notesClose
        onClicked:
        {
          if (overlay.updater)
            overlay.updater.applyUpdate()
        }
      }

      Btn
      {
        id: notesHow
        label: qsTr("How to update")
        hint: qsTr("Shows the command that installs an update")
        visible: !overlay.haveUpdate
        primary: true
        minW: 300
        KeyNavigation.right: notesClose
        KeyNavigation.tab: notesClose
        onClicked:
        {
          if (overlay.updater)
            overlay.updater.showManual()
        }
      }

      Btn
      {
        id: notesClose
        label: qsTr("Close")
        hint: overlay.haveUpdate
              ? qsTr("Leaves the update for later. It is offered again next time Plex starts")
              : qsTr("Closes the release notes")
        minW: 200
        KeyNavigation.left: overlay.haveUpdate ? notesApply : notesHow
        KeyNavigation.right: overlay.haveUpdate ? notesSkip : notesNever
        KeyNavigation.tab: overlay.haveUpdate ? notesSkip : notesNever
        onClicked:
        {
          if (!overlay.updater)
            overlay.close()
          else if (overlay.infoOpen)
            overlay.updater.hideInfo()
          else
            overlay.updater.dismiss()
        }
      }
    }

    Row
    {
      spacing: Math.round(24 * overlay.u)
      anchors.right: parent.right
      anchors.rightMargin: dialog.pad
      anchors.verticalCenter: notesButtons.verticalCenter

      Btn
      {
        id: notesSkip
        label: qsTr("Skip this version")
        hint: qsTr("Does not offer this version again. A later one is still offered")
        visible: overlay.haveUpdate
        minW: 300
        KeyNavigation.left: notesClose
        KeyNavigation.right: notesNever
        KeyNavigation.tab: notesNever
        onClicked:
        {
          if (overlay.updater)
            overlay.updater.skipVersion()
        }
      }

      Btn
      {
        id: notesNever
        label: qsTr("Never")
        hint: qsTr("Turns update checking off, after a confirmation")
        minW: 200
        KeyNavigation.left: overlay.haveUpdate ? notesSkip : notesClose
        KeyNavigation.tab: checkToggle
        onClicked:
        {
          overlay.view = overlay.viewConfirm
          Qt.callLater(overlay.takeFocus)
        }
      }
    }
  }

  Card
  {
    id: confirm
    visible: overlay.showConfirm
    width: Math.round(900 * overlay.u)
    height: confirm.pad * 2 + confirmTitle.height + Math.round(20 * overlay.u)
            + confirmBody.height + Math.round(40 * overlay.u) + confirmButtons.height
    anchors.centerIn: parent

    property real pad: Math.round(48 * overlay.u)

    Accessible.role: Accessible.Dialog
    Accessible.name: qsTr("Stop checking for updates?")

    Text
    {
      id: confirmTitle
      x: confirm.pad
      y: confirm.pad
      text: qsTr("Stop checking for updates?")
      color: overlay.textPri
      font.pixelSize: Math.round(40 * overlay.u)
      font.weight: Font.Bold
    }

    Text
    {
      id: confirmBody
      x: confirm.pad
      width: confirm.width - confirm.pad * 2
      anchors.top: confirmTitle.bottom
      anchors.topMargin: Math.round(20 * overlay.u)
      wrapMode: Text.WordWrap
      textFormat: Text.PlainText
      lineHeight: 1.45
      text: qsTr("Plex will not offer updates again. The next screen shows how to "
                 + "install one from a terminal, and how to turn checking back on.")
      color: overlay.textSec
      font.pixelSize: Math.round(26 * overlay.u)
    }

    Row
    {
      id: confirmButtons
      spacing: Math.round(24 * overlay.u)
      anchors.right: parent.right
      anchors.bottom: parent.bottom
      anchors.margins: confirm.pad

      Btn
      {
        id: confirmYes
        label: qsTr("Yes, stop")
        minW: 240
        KeyNavigation.right: confirmNo
        KeyNavigation.tab: confirmNo
        onClicked:
        {
          if (overlay.updater)
            overlay.updater.disableChecks()
        }
      }

      Btn
      {
        id: confirmNo
        label: qsTr("No, keep checking")
        primary: true
        minW: 320
        KeyNavigation.left: confirmYes
        KeyNavigation.tab: confirmYes
        onClicked: overlay.close()
      }
    }
  }

  Card
  {
    id: progress
    visible: overlay.showProgress
    width: Math.min(Math.round(1180 * overlay.u), overlay.width * 0.8)
    height: Math.round(240 * overlay.u)
    anchors.centerIn: parent

    property real pad: Math.round(48 * overlay.u)
    property int total: overlay.updater ? Math.max(1, overlay.updater.opCount) : 1
    property int step: overlay.updater ? Math.min(total, overlay.updater.opIndex + 1) : 1
    property int percent: overlay.updater ? Math.max(0, Math.min(100, overlay.updater.opProgress)) : 0

    Accessible.role: Accessible.ProgressBar
    Accessible.name: qsTr("Updating")
    Accessible.description: qsTr("step %1 of %2, %3 percent").arg(progress.step)
                                                             .arg(progress.total)
                                                             .arg(progress.percent)

    Text
    {
      id: progressTitle
      x: progress.pad
      y: progress.pad
      text: qsTr("Updating…")
      color: overlay.textPri
      font.pixelSize: Math.round(36 * overlay.u)
      font.weight: Font.DemiBold
    }

    Text
    {
      anchors.right: parent.right
      anchors.rightMargin: progress.pad
      anchors.baseline: progressTitle.baseline
      text: qsTr("step %1 of %2   ·   %3%").arg(progress.step)
                                               .arg(progress.total)
                                               .arg(progress.percent)
      color: overlay.textSec
      font.pixelSize: Math.round(26 * overlay.u)
    }

    Rectangle
    {
      x: progress.pad
      width: progress.width - progress.pad * 2
      height: Math.round(24 * overlay.u)
      radius: height / 2
      color: overlay.track
      anchors.bottom: parent.bottom
      anchors.bottomMargin: progress.pad

      Rectangle
      {
        width: parent.width * (progress.percent / 100)
        height: parent.height
        radius: height / 2
        color: overlay.accentFill

        Behavior on width
        {
          NumberAnimation { duration: 150 }
        }
      }
    }
  }

  Card
  {
    id: done
    visible: overlay.showDone
    width: Math.min(Math.round(1180 * overlay.u), overlay.width * 0.8)
    height: Math.round(300 * overlay.u)
    anchors.centerIn: parent

    property real pad: Math.round(48 * overlay.u)

    Accessible.role: Accessible.Dialog
    Accessible.name: doneLabel.text

    Text
    {
      id: doneLabel
      x: done.pad
      y: done.pad
      width: done.width - done.pad * 2
      wrapMode: Text.WordWrap
      textFormat: Text.PlainText
      text: overlay.updater
            ? qsTr("%1 installed. Restart Plex to use it.").arg(overlay.updater.availableVersion)
            : ""
      color: overlay.textPri
      font.pixelSize: Math.round(36 * overlay.u)
      font.weight: Font.DemiBold
    }

    Row
    {
      spacing: Math.round(24 * overlay.u)
      x: done.pad
      anchors.bottom: parent.bottom
      anchors.bottomMargin: done.pad

      Btn
      {
        id: doneRestart
        label: qsTr("Restart app now")
        hint: qsTr("Closes Plex and starts the installed version")
        primary: true
        minW: 300
        KeyNavigation.right: doneLater
        KeyNavigation.tab: doneLater
        onClicked:
        {
          if (overlay.updater)
            overlay.updater.restartApp()
        }
      }

      Btn
      {
        id: doneLater
        label: qsTr("On next launch")
        hint: qsTr("Keeps using this version until Plex is started again")
        minW: 280
        KeyNavigation.left: doneRestart
        KeyNavigation.tab: doneRestart
        onClicked:
        {
          if (overlay.updater)
            overlay.updater.dismiss()
        }
      }
    }
  }

  Card
  {
    id: manual
    visible: overlay.showManual
    width: Math.min(Math.round(1180 * overlay.u), overlay.width * 0.8)
    height: Math.min(overlay.height - overlay.inset * 2,
                     manual.pad * 2 + manualTitle.height
                     + (manualError.visible ? manualError.height + Math.round(14 * overlay.u) : 0)
                     + (manual.perm ? Math.round(26 * overlay.u) + permCommand.height
                                      + Math.round(26 * overlay.u) + manualOr.height : 0)
                     + Math.round(26 * overlay.u) + manualCommand.height
                     + (manualReenable.visible ? Math.round(26 * overlay.u) + manualReenable.height : 0)
                     + Math.round(40 * overlay.u) + manualClose.height)
    anchors.centerIn: parent

    property real pad: Math.round(48 * overlay.u)
    property bool perm: overlay.updater !== null && overlay.updater.needsPermission

    Accessible.role: Accessible.Dialog
    Accessible.name: manualTitle.text

    Text
    {
      id: manualTitle
      x: manual.pad
      y: manual.pad
      width: manual.width - manual.pad * 2
      wrapMode: Text.WordWrap
      textFormat: Text.PlainText
      lineHeight: 1.45
      color: overlay.textPri
      font.pixelSize: Math.round(32 * overlay.u)
      text:
      {
        if (!overlay.updater)
          return ""
        if (manual.perm)
          return qsTr("Plex is not allowed to install its own updates on this system yet. Run this once from a terminal, then try the update again:")
        if (overlay.updateState === overlay.stateFailed)
          return qsTr("The update could not be installed. Install it from a terminal with:")
        if (overlay.updater.updatePending)
          return qsTr("%1 is available, but has to be installed from a terminal:")
                 .arg(overlay.updater.availableVersion)
        return qsTr("Updates are installed from a terminal with:")
      }
    }

    Text
    {
      id: manualError
      x: manual.pad
      width: manual.width - manual.pad * 2
      anchors.top: manualTitle.bottom
      anchors.topMargin: Math.round(14 * overlay.u)
      visible: overlay.updateState === overlay.stateFailed
               && overlay.updater !== null
               && overlay.updater.errorMessage.length > 0
      wrapMode: Text.WordWrap
      textFormat: Text.PlainText
      maximumLineCount: 3
      elide: Text.ElideRight
      text: overlay.updater ? overlay.updater.errorMessage : ""
      color: overlay.textSec
      font.pixelSize: Math.round(24 * overlay.u)
    }

    Rectangle
    {
      id: permCommand
      x: manual.pad
      width: manual.width - manual.pad * 2
      height: Math.max(Math.round(96 * overlay.u), permText.height + Math.round(44 * overlay.u))
      radius: Math.round(8 * overlay.u)
      color: overlay.panel
      border.width: Math.max(1, Math.round(2 * overlay.u))
      border.color: overlay.outline
      visible: manual.perm
      anchors.top: manualError.visible ? manualError.bottom : manualTitle.bottom
      anchors.topMargin: Math.round(26 * overlay.u)

      Text
      {
        id: permText
        anchors.verticalCenter: parent.verticalCenter
        x: Math.round(28 * overlay.u)
        width: parent.width - Math.round(56 * overlay.u)
        wrapMode: Text.WrapAnywhere
        textFormat: Text.PlainText
        text: overlay.updater ? overlay.updater.permissionCommand : ""
        color: overlay.textPri
        font.family: "monospace"
        font.pixelSize: Math.round(22 * overlay.u)
      }
    }

    Text
    {
      id: manualOr
      x: manual.pad
      width: manual.width - manual.pad * 2
      anchors.top: permCommand.bottom
      anchors.topMargin: Math.round(26 * overlay.u)
      visible: manual.perm
      wrapMode: Text.WordWrap
      textFormat: Text.PlainText
      lineHeight: 1.45
      text: qsTr("Or install this update directly:")
      color: overlay.textSec
      font.pixelSize: Math.round(24 * overlay.u)
    }

    Rectangle
    {
      id: manualCommand
      x: manual.pad
      width: manual.width - manual.pad * 2
      height: Math.round(96 * overlay.u)
      radius: Math.round(8 * overlay.u)
      color: overlay.panel
      border.width: Math.max(1, Math.round(2 * overlay.u))
      border.color: overlay.outline
      anchors.top: manual.perm ? manualOr.bottom
                               : (manualError.visible ? manualError.bottom : manualTitle.bottom)
      anchors.topMargin: Math.round(26 * overlay.u)

      Text
      {
        anchors.verticalCenter: parent.verticalCenter
        x: Math.round(28 * overlay.u)
        width: parent.width - Math.round(56 * overlay.u)
        elide: Text.ElideRight
        textFormat: Text.PlainText
        text: overlay.updater ? overlay.updater.manualCommand : ""
        color: overlay.textPri
        font.family: "monospace"
        font.pixelSize: Math.round(26 * overlay.u)
      }
    }

    Text
    {
      id: manualReenable
      x: manual.pad
      width: manual.width - manual.pad * 2
      anchors.top: manualCommand.bottom
      anchors.topMargin: Math.round(26 * overlay.u)
      visible: overlay.updater !== null && !overlay.updater.checkEnabled
      wrapMode: Text.WordWrap
      textFormat: Text.PlainText
      lineHeight: 1.45
      text: qsTr("To have Plex offer updates again, start it once with --update-check.")
      color: overlay.textSec
      font.pixelSize: Math.round(24 * overlay.u)
    }

    Btn
    {
      id: manualClose
      label: qsTr("Close")
      minW: 200
      anchors.right: parent.right
      anchors.bottom: parent.bottom
      anchors.margins: manual.pad
      onClicked:
      {
        if (overlay.updater)
          overlay.updater.dismiss()
      }
    }
  }
}
