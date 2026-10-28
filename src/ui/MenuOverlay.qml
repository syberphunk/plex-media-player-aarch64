import QtQuick

FocusScope
{
  id: menu

  property var cfg: (typeof components !== "undefined") ? components.menu : null
  property var settings: (typeof components !== "undefined") ? components.settings : null
  property var updater: (typeof components !== "undefined") ? components.updater : null
  property var system: (typeof components !== "undefined") ? components.system : null
  property var steam: (typeof components !== "undefined") ? components.steam : null
  property var xr: (typeof components !== "undefined") ? components.xr : null

  readonly property bool signedIn: system ? system.signedIn : false

  readonly property bool open: cfg ? cfg.visible : false
  readonly property bool contrast: cfg ? cfg.highContrast : false
  readonly property real fontScale: cfg ? cfg.textScale : 1.0

  readonly property int stateAvailable: 1
  readonly property int checkBusy: 1
  readonly property int checkUpToDate: 2
  readonly property int checkFailed: 3

  property real u: Math.max(0.6, Math.min(2.0, menu.height / 1080))
  property real inset: Math.round(menu.height * 0.035)

  readonly property string surface:    contrast ? "#000000" : "#181A1F"
  readonly property string panel:      contrast ? "#000000" : "#22252B"
  readonly property string textPri:    contrast ? "#FFFFFF" : "#ECEEF1"
  readonly property string textSec:    contrast ? "#FFFFFF" : "#B9BFC8"
  readonly property string accentText: contrast ? "#FFD24A" : "#E79A3C"
  readonly property string accentFill: contrast ? "#FFD24A" : "#E08B20"
  readonly property string onAccent:   "#101216"
  readonly property string outline:    contrast ? "#FFFFFF" : "#767E8A"
  readonly property string ringOuter:  "#F2F4F7"
  readonly property string ringInner:  "#0B0D10"
  readonly property string track:      contrast ? "#000000" : "#2A2E35"
  readonly property string rule:       contrast ? "#FFFFFF" : "#2F333A"

  property var pendingDownload: null
  property string pendingName: ""

  property bool customPathOpen: false
  property string customPath: ""

  property bool steamOffer: false
  property bool steamOfferShown: false
  property string steamStatus: ""

  signal closed()
  signal signIn()
  signal onlineMediaSources()

  visible: opacity > 0
  opacity: open ? 1 : 0

  Behavior on opacity
  {
    NumberAnimation { duration: 120 }
  }

  onOpenChanged:
  {
    if (cfg)
      cfg.grabKeyboard("menu", open)

    if (open)
    {
      if (steam)
        steam.refresh()

      customPathOpen = cfg && cfg.downloadPreset === "custom"
      customPath = cfg ? cfg.downloadDir : ""
      Qt.callLater(menu.takeFocus)
      return;
    }

    menu.steamOffer = false
    menu.closed()
  }

  function takeFocus()
  {
    if (steamOffer)
      steamCreateButton.forceActiveFocus()
    else if (pendingDownload)
      askSave.forceActiveFocus()
    else
      checkButton.forceActiveFocus()
  }

  function dismiss()
  {
    if (picker.visible)
    {
      picker.close()
      return;
    }

    if (cfg)
      cfg.hide()
  }

  function fontPx(size)
  {
    return Math.round(size * menu.u * menu.fontScale)
  }

  function step(item, forward)
  {
    var next = item.nextItemInFocusChain(forward)
    var probe = next

    while (probe)
    {
      if (probe === sheet)
      {
        next.forceActiveFocus()
        return;
      }

      probe = probe.parent
    }
  }

  function reveal(item)
  {
    var probe = item

    while (probe && probe !== list)
      probe = probe.parent

    if (!probe)
      return;

    var top = item.mapToItem(list, 0, 0).y
    var bottom = top + item.height

    if (top - Math.round(24 * menu.u) < flick.contentY)
      flick.contentY = Math.max(0, top - Math.round(24 * menu.u))
    else if (bottom + Math.round(24 * menu.u) > flick.contentY + flick.height)
      flick.contentY = Math.min(Math.max(0, flick.contentHeight - flick.height),
                                bottom + Math.round(24 * menu.u) - flick.height)
  }

  function openSignIn()
  {
    if (cfg)
      cfg.hide()

    menu.signIn()
  }

  function openOnlineMediaSources()
  {
    if (cfg)
      cfg.hide()

    menu.onlineMediaSources()
  }

  Timer
  {
    id: steamOfferTimer
    interval: 6000
    repeat: false
    onTriggered: menu.raiseSteamOffer()
  }

  Component.onCompleted:
  {
    if (menu.steamOfferAllowed())
      steamOfferTimer.start()
  }

  onSignedInChanged:
  {
    if (menu.steamOfferAllowed())
      steamOfferTimer.restart()
  }

  function raiseSteamOffer()
  {
    if (!menu.steamOfferAllowed())
      return;

    menu.steamOfferShown = true
    menu.steamStatus = ""
    menu.steamOffer = true

    if (cfg && !cfg.visible)
      cfg.show()
  }

  function steamOfferAllowed()
  {
    if (!steam || !settings || menu.steamOfferShown || !menu.signedIn)
      return false;

    return steam.shouldOffer && settings.value("main", "steamShortcutOffer")
  }

  function createSteamShortcut()
  {
    if (!steam)
      return;

    if (steam.createShortcut())
    {
      menu.steamStatus = qsTr("Icon created. Steam reads its shortcut list when it "
                              + "starts, so restart Steam and then open Plex from "
                              + "your library.")
    }
    else
    {
      menu.steamStatus = steam.lastError.length > 0
                           ? steam.lastError
                           : qsTr("The Steam icon could not be created.")
    }

    menu.steamOffer = false
  }

  function relaunchViaSteam()
  {
    if (!steam)
      return;

    menu.steamOffer = false

    if (!steam.relaunchThroughSteam())
    {
      menu.steamStatus = steam.lastError.length > 0
                           ? steam.lastError
                           : qsTr("Steam did not answer.")
      return;
    }

    menu.steamStatus = qsTr("Asked Steam to start Plex.")

    if (cfg)
      cfg.hide()

    if (system)
      system.exit()
  }

  function xrStatusText()
  {
    if (!menu.xr)
      return ""
    if (menu.xr.busy)
      return qsTr("Asking the headset runtime...")
    if (!menu.xr.checked)
      return qsTr("Not checked yet.")
    if (!menu.xr.runtimeFound)
      return menu.xr.lastError.length > 0
               ? qsTr("No headset runtime: %1").arg(menu.xr.lastError)
               : qsTr("No headset runtime answered.")
    if (!menu.xr.headsetPresent)
      return qsTr("%1 is running but reports no headset. Put the headset on and "
                  + "check again.").arg(menu.xr.runtimeName)
    var text = qsTr("%1, %2 per eye").arg(menu.xr.headsetName.length > 0
                                            ? menu.xr.headsetName
                                            : menu.xr.runtimeName)
                                     .arg(menu.xr.perEye)
    if (menu.xr.sessionReady)
      return text + qsTr(". Both eyes were drawn to.")
    if (menu.xr.lastError.length > 0)
      return text + qsTr(". The last check failed: %1").arg(menu.xr.lastError)
    return text + qsTr(". Not drawn to yet.")
  }

  function packingLabel(value)
  {
    if (value === "sbs_half")
      return qsTr("Side-by-side, half width")
    if (value === "sbs")
      return qsTr("Side-by-side, full width")
    if (value === "ou_half")
      return qsTr("Over-under, half height")
    if (value === "ou")
      return qsTr("Over-under, full height")
    if (value === "mono")
      return qsTr("Treat everything as 2D")
    return qsTr("Work it out per file")
  }

  function packingStatusText()
  {
    if (!menu.xr)
      return ""
    if (menu.xr.detectedLabel.length === 0)
      return qsTr("Nothing is playing.")

    var from = menu.xr.detectedSource
    if (from === "override")
      return qsTr("Playing: %1, because this menu says so.").arg(menu.xr.detectedLabel)
    if (from === "metadata")
      return qsTr("Playing: %1, from the file's own 3D tag.").arg(menu.xr.detectedLabel)
    if (from === "name")
      return qsTr("Playing: %1, worked out from the file name.").arg(menu.xr.detectedLabel)
    if (from === "aspect")
      return qsTr("Playing: %1, worked out from the picture shape. Set it here if "
                  + "that is wrong.").arg(menu.xr.detectedLabel)
    if (from === "unclear")
      return qsTr("The name says this file is 3D but not how it is packed, and it "
                  + "carries no 3D tag. Set it here: half width is the usual one.")
    return qsTr("Playing: %1. No 3D tag, nothing in the name, and the shape looks "
                + "ordinary. Set it here if this file is 3D.").arg(menu.xr.detectedLabel)
  }

  function dismissSteamOffer(forever)
  {
    menu.steamOffer = false

    if (forever && settings)
      settings.setValue("main", "steamShortcutOffer", false)

    if (cfg)
      cfg.hide()
  }

  function openReleaseNotes()
  {
    if (!updater)
      return;

    if (cfg)
      cfg.hide()

    updater.showInfo()
  }

  function cadenceLabel(value)
  {
    if (value === "daily")
      return qsTr("Once a day")
    if (value === "weekly")
      return qsTr("Once a week")
    if (value === "manual")
      return qsTr("Only when I ask")

    return qsTr("Every time Plex starts")
  }

  function presetLabel(value)
  {
    if (value === "appdata")
      return qsTr("Plex's own data folder")
    if (value === "custom")
      return qsTr("A folder I choose")

    return qsTr("Downloads folder")
  }

  function textSizeLabel(value)
  {
    if (value === "large")
      return qsTr("Large")
    if (value === "larger")
      return qsTr("Larger")

    return qsTr("Normal")
  }

  function applyPreset(value)
  {
    if (!cfg)
      return;

    if (value === "custom")
    {
      customPath = cfg.downloadDir
      customPathOpen = true
      Qt.callLater(function() { pathInput.forceActiveFocus() })
      return;
    }

    customPathOpen = false
    cfg.applyDownloadDir(value === "appdata" ? cfg.presetPath("appdata") : "")
  }

  function askDownload(request)
  {
    pendingDownload = request
    pendingName = request.downloadFileName

    if (cfg)
      cfg.show()

    Qt.callLater(menu.takeFocus)
  }

  function resolvePending(save)
  {
    var request = pendingDownload
    pendingDownload = null

    if (!request)
      return;

    if (save)
    {
      request.downloadDirectory = cfg ? cfg.downloadDir : request.downloadDirectory
      request.accept()

      if (cfg)
        cfg.noteDownload(pendingName, "started")
    }
    else
    {
      request.cancel()

      if (cfg)
        cfg.noteDownload(pendingName, "cancelled")
    }

    pendingName = ""
    Qt.callLater(menu.takeFocus)
  }

  Keys.onEscapePressed: menu.dismiss()
  Keys.onBackPressed: menu.dismiss()

  component Btn : Item
  {
    id: btn

    property string label: ""
    property string hint: ""
    property bool primary: false
    property real minW: 240

    signal clicked()

    implicitWidth: Math.max(Math.round(minW * menu.u),
                            btnLabel.implicitWidth + Math.round(56 * menu.u))
    implicitHeight: Math.round(64 * menu.u)
    activeFocusOnTab: true

    Accessible.role: Accessible.Button
    Accessible.name: btn.label
    Accessible.description: btn.hint
    Accessible.onPressAction: btn.clicked()

    Keys.onReturnPressed: btn.clicked()
    Keys.onEnterPressed: btn.clicked()
    Keys.onSpacePressed: btn.clicked()
    Keys.onUpPressed: menu.step(btn, false)
    Keys.onDownPressed: menu.step(btn, true)

    onActiveFocusChanged:
    {
      if (activeFocus)
        menu.reveal(btn)
    }

    Rectangle
    {
      anchors.fill: parent
      radius: Math.round(8 * menu.u)
      color: btn.primary ? menu.accentFill : "transparent"
      border.width: btn.primary ? 0 : Math.max(1, Math.round(2 * menu.u))
      border.color: menu.outline

      Text
      {
        id: btnLabel
        anchors.centerIn: parent
        text: btn.label
        color: btn.primary ? menu.onAccent : menu.textPri
        font.pixelSize: menu.fontPx(30)
        font.weight: Font.DemiBold
      }
    }

    Rectangle
    {
      visible: btn.activeFocus
      anchors.fill: parent
      anchors.margins: -Math.round(8 * menu.u)
      radius: Math.round(16 * menu.u)
      color: "transparent"
      border.width: Math.round(4 * menu.u)
      border.color: menu.ringOuter
    }

    Rectangle
    {
      visible: btn.activeFocus
      anchors.fill: parent
      anchors.margins: -Math.round(3 * menu.u)
      radius: Math.round(11 * menu.u)
      color: "transparent"
      border.width: Math.round(3 * menu.u)
      border.color: menu.ringInner
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
    radius: Math.round(14 * menu.u)
    color: menu.surface
    border.width: Math.max(1, Math.round(2 * menu.u))
    border.color: menu.outline
  }

  component SettingRow : Item
  {
    id: row

    property string label: ""
    property string hint: ""
    property bool interactive: true

    signal activated()

    implicitHeight: Math.max(Math.round(76 * menu.u),
                             rowLabel.implicitHeight + rowHint.implicitHeight
                               + Math.round(28 * menu.u))
    activeFocusOnTab: row.interactive

    Accessible.name: row.label
    Accessible.description: row.hint

    Keys.onReturnPressed: row.activated()
    Keys.onEnterPressed: row.activated()
    Keys.onSpacePressed: row.activated()
    Keys.onUpPressed: menu.step(row, false)
    Keys.onDownPressed: menu.step(row, true)

    onActiveFocusChanged:
    {
      if (activeFocus)
        menu.reveal(row)
    }

    Rectangle
    {
      anchors.fill: parent
      radius: Math.round(8 * menu.u)
      color: row.activeFocus ? menu.track : "transparent"
    }

    Rectangle
    {
      visible: row.activeFocus
      anchors.fill: parent
      radius: Math.round(8 * menu.u)
      color: "transparent"
      border.width: Math.round(3 * menu.u)
      border.color: menu.ringOuter
    }

    Text
    {
      id: rowLabel
      anchors.left: parent.left
      anchors.leftMargin: Math.round(18 * menu.u)
      anchors.top: parent.top
      anchors.topMargin: Math.round(12 * menu.u)
      width: parent.width * 0.52
      text: row.label
      color: menu.textPri
      font.pixelSize: menu.fontPx(28)
      font.weight: Font.DemiBold
      wrapMode: Text.WordWrap
    }

    Text
    {
      id: rowHint
      anchors.left: rowLabel.left
      anchors.top: rowLabel.bottom
      anchors.topMargin: row.hint.length > 0 ? Math.round(4 * menu.u) : 0
      width: rowLabel.width
      text: row.hint
      visible: row.hint.length > 0
      color: menu.textSec
      font.pixelSize: menu.fontPx(22)
      wrapMode: Text.WordWrap
    }

    MouseArea
    {
      anchors.fill: parent
      enabled: row.interactive
      cursorShape: Qt.PointingHandCursor
      onClicked:
      {
        row.forceActiveFocus()
        row.activated()
      }
    }
  }

  component Toggle : SettingRow
  {
    id: toggle

    property bool checked: false

    signal toggled(bool value)

    Accessible.role: Accessible.CheckBox
    Accessible.checked: toggle.checked
    Accessible.onToggleAction: toggle.activated()

    onActivated: toggle.toggled(!toggle.checked)

    Rectangle
    {
      anchors.right: parent.right
      anchors.rightMargin: Math.round(24 * menu.u)
      anchors.verticalCenter: parent.verticalCenter
      width: Math.round(44 * menu.u)
      height: width
      radius: Math.round(6 * menu.u)
      color: toggle.checked ? menu.accentFill : "transparent"
      border.width: Math.max(1, Math.round(2 * menu.u))
      border.color: menu.outline

      Text
      {
        anchors.centerIn: parent
        visible: toggle.checked
        text: "✓"
        color: menu.onAccent
        font.pixelSize: Math.round(30 * menu.u)
        font.weight: Font.Bold
      }
    }
  }

  component Picker : SettingRow
  {
    id: pick

    property var options: []
    property string current: ""
    property string currentLabel: ""

    signal picked(string value)

    Accessible.role: Accessible.ComboBox

    onActivated: picker.openFor(pick)

    Keys.onLeftPressed: pick.cycle(-1)
    Keys.onRightPressed: pick.cycle(1)

    function indexOfCurrent()
    {
      for (var i = 0; i < pick.options.length; i++)
      {
        if (pick.options[i].value === pick.current)
          return i;
      }

      return 0;
    }

    function cycle(delta)
    {
      if (!pick.options.length)
        return;

      var next = (pick.indexOfCurrent() + delta + pick.options.length) % pick.options.length
      pick.picked(pick.options[next].value)
    }

    Text
    {
      anchors.right: parent.right
      anchors.rightMargin: Math.round(24 * menu.u)
      anchors.verticalCenter: parent.verticalCenter
      width: parent.width * 0.4
      horizontalAlignment: Text.AlignRight
      elide: Text.ElideRight
      text: (pick.currentLabel.length > 0 ? pick.currentLabel : pick.current) + "  ▾"
      color: menu.accentText
      font.pixelSize: menu.fontPx(26)
      font.weight: Font.DemiBold
    }
  }

  component Heading : Item
  {
    id: head

    property string text: ""

    implicitHeight: Math.round(84 * menu.u)

    Accessible.role: Accessible.Heading
    Accessible.name: head.text

    Text
    {
      id: headText
      anchors.left: parent.left
      anchors.leftMargin: Math.round(18 * menu.u)
      anchors.bottom: headRule.top
      anchors.bottomMargin: Math.round(10 * menu.u)
      text: head.text
      color: menu.textPri
      font.pixelSize: menu.fontPx(32)
      font.weight: Font.Bold
    }

    Rectangle
    {
      id: headRule
      anchors.left: parent.left
      anchors.right: parent.right
      anchors.bottom: parent.bottom
      anchors.bottomMargin: Math.round(8 * menu.u)
      height: Math.max(1, Math.round(2 * menu.u))
      color: menu.rule
    }
  }

  component Note : Text
  {
    color: menu.textSec
    font.pixelSize: menu.fontPx(24)
    wrapMode: Text.WordWrap
  }

  Rectangle
  {
    anchors.fill: parent
    color: "black"
    opacity: 0.72

    MouseArea
    {
      anchors.fill: parent
      hoverEnabled: true
      acceptedButtons: Qt.AllButtons
      onClicked: (mouse) =>
      {
        if (mouse.button === Qt.LeftButton)
          menu.dismiss()
      }
    }
  }

  Card
  {
    id: sheet

    anchors.centerIn: parent
    width: Math.min(Math.round(1240 * menu.u), menu.width - 2 * menu.inset)
    height: menu.height - 2 * menu.inset
    color: menu.panel

    Accessible.role: Accessible.Dialog
    Accessible.name: qsTr("Plex Media Player settings")

    Text
    {
      id: title
      anchors.left: parent.left
      anchors.top: parent.top
      anchors.leftMargin: Math.round(36 * menu.u)
      anchors.topMargin: Math.round(28 * menu.u)
      text: qsTr("Plex Media Player settings")
      color: menu.textPri
      font.pixelSize: menu.fontPx(40)
      font.weight: Font.Bold
    }

    Text
    {
      id: subtitle
      anchors.left: title.left
      anchors.top: title.bottom
      anchors.topMargin: Math.round(6 * menu.u)
      text: cfg ? qsTr("%1 opens and closes this menu").arg(cfg.menuKey) : ""
      color: menu.textSec
      font.pixelSize: menu.fontPx(24)
    }

    Card
    {
      id: askBar

      visible: menu.pendingDownload !== null
      anchors.left: parent.left
      anchors.right: parent.right
      anchors.top: subtitle.bottom
      anchors.leftMargin: Math.round(28 * menu.u)
      anchors.rightMargin: Math.round(28 * menu.u)
      anchors.topMargin: Math.round(18 * menu.u)
      height: visible ? Math.round(150 * menu.u) : 0
      color: menu.surface
      border.color: menu.accentFill

      Accessible.role: Accessible.Pane
      Accessible.name: qsTr("Download waiting")

      Text
      {
        id: askText
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: Math.round(24 * menu.u)
        anchors.topMargin: Math.round(18 * menu.u)
        width: parent.width - Math.round(48 * menu.u)
        text: qsTr("Save %1 to %2?").arg(menu.pendingName).arg(cfg ? cfg.downloadDir : "")
        color: menu.textPri
        font.pixelSize: menu.fontPx(26)
        elide: Text.ElideMiddle
      }

      Btn
      {
        id: askSave
        anchors.left: askText.left
        anchors.top: askText.bottom
        anchors.topMargin: Math.round(14 * menu.u)
        label: qsTr("Save")
        hint: qsTr("Save the file to the folder named above")
        primary: true
        minW: 180
        onClicked: menu.resolvePending(true)
      }

      Btn
      {
        id: askCancel
        anchors.left: askSave.right
        anchors.leftMargin: Math.round(16 * menu.u)
        anchors.verticalCenter: askSave.verticalCenter
        label: qsTr("Cancel")
        hint: qsTr("Do not download the file")
        minW: 180
        onClicked: menu.resolvePending(false)
      }
    }

    Card
    {
      id: steamBar

      visible: menu.steamOffer
      anchors.left: parent.left
      anchors.right: parent.right
      anchors.top: askBar.visible ? askBar.bottom : subtitle.bottom
      anchors.leftMargin: Math.round(28 * menu.u)
      anchors.rightMargin: Math.round(28 * menu.u)
      anchors.topMargin: Math.round(18 * menu.u)
      height: visible ? Math.round(214 * menu.u) : 0
      color: menu.surface
      border.color: menu.accentFill

      Accessible.role: Accessible.Pane
      Accessible.name: qsTr("Steam icon")

      Text
      {
        id: steamOfferText
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: Math.round(24 * menu.u)
        anchors.topMargin: Math.round(18 * menu.u)
        width: parent.width - Math.round(48 * menu.u)
        text: qsTr("Looks like you are on a Steam Frame. Plex runs better started "
                   + "from Steam, which is what gives it the performance profile "
                   + "the device reserves for games. Add a Steam icon for it?")
        color: menu.textPri
        font.pixelSize: menu.fontPx(26)
        wrapMode: Text.WordWrap
      }

      Btn
      {
        id: steamCreateButton
        anchors.left: steamOfferText.left
        anchors.top: steamOfferText.bottom
        anchors.topMargin: Math.round(14 * menu.u)
        label: qsTr("Add icon")
        hint: qsTr("Write a Plex entry into your Steam library")
        primary: true
        minW: 200
        onClicked: menu.createSteamShortcut()
      }

      Btn
      {
        id: steamLaterButton
        anchors.left: steamCreateButton.right
        anchors.leftMargin: Math.round(16 * menu.u)
        anchors.verticalCenter: steamCreateButton.verticalCenter
        label: qsTr("Not now")
        hint: qsTr("Leave Steam alone and ask again next time")
        minW: 200
        onClicked: menu.dismissSteamOffer(false)
      }

      Btn
      {
        anchors.left: steamLaterButton.right
        anchors.leftMargin: Math.round(16 * menu.u)
        anchors.verticalCenter: steamCreateButton.verticalCenter
        label: qsTr("Never ask")
        hint: qsTr("Stop offering this, the icon can still be added from this menu")
        minW: 200
        onClicked: menu.dismissSteamOffer(true)
      }
    }

    Flickable
    {
      id: flick

      anchors.left: parent.left
      anchors.right: parent.right
      anchors.top: steamBar.visible ? steamBar.bottom
                                    : (askBar.visible ? askBar.bottom : subtitle.bottom)
      anchors.bottom: footer.top
      anchors.leftMargin: Math.round(28 * menu.u)
      anchors.rightMargin: Math.round(34 * menu.u)
      anchors.topMargin: Math.round(18 * menu.u)
      anchors.bottomMargin: Math.round(12 * menu.u)
      clip: true
      contentWidth: width
      contentHeight: list.implicitHeight
      boundsBehavior: Flickable.StopAtBounds

      Behavior on contentY
      {
        enabled: !flick.moving && !scrollDrag.drag.active
        NumberAnimation { duration: 160; easing.type: Easing.OutCubic }
      }

      Column
      {
        id: list
        width: flick.width
        spacing: Math.round(4 * menu.u)

        Heading
        {
          width: parent.width
          text: qsTr("Account")
        }

        Note
        {
          width: parent.width - Math.round(36 * menu.u)
          x: Math.round(18 * menu.u)
          visible: !menu.signedIn
          text: qsTr("Use this if Plex opens on an empty screen with a loading wheel. "
                     + "That is how a signed-out client looks, and it offers no way "
                     + "back to sign-in on its own.")
        }

        Item
        {
          width: parent.width
          visible: !menu.signedIn
          height: visible ? Math.round(96 * menu.u) : 0

          Btn
          {
            id: signInButton
            anchors.left: parent.left
            anchors.leftMargin: Math.round(18 * menu.u)
            anchors.verticalCenter: parent.verticalCenter
            label: qsTr("Sign in to Plex")
            hint: qsTr("Load the Plex sign-in screen")
            minW: 330
            onClicked: menu.openSignIn()
          }
        }

        Note
        {
          width: parent.width - Math.round(36 * menu.u)
          x: Math.round(18 * menu.u)
          visible: menu.signedIn
          text: qsTr("The Trending and Activity tabs on Home are not served to "
                     + "this client and report an error when opened. Turning off "
                     + "Discover Source removes them. This is a Plex account "
                     + "setting and applies to every Plex app you sign in to.")
        }

        Item
        {
          width: parent.width
          visible: menu.signedIn
          height: visible ? Math.round(96 * menu.u) : 0

          Btn
          {
            id: onlineMediaSourcesButton
            anchors.left: parent.left
            anchors.leftMargin: Math.round(18 * menu.u)
            anchors.verticalCenter: parent.verticalCenter
            label: qsTr("Online Media Sources")
            hint: qsTr("Open the Plex page for Discover and other online sources")
            minW: 330
            onClicked: menu.openOnlineMediaSources()
          }
        }

        Heading
        {
          width: parent.width
          text: qsTr("Updates")
        }

        Note
        {
          width: parent.width - Math.round(36 * menu.u)
          x: Math.round(18 * menu.u)
          text: updater
                ? qsTr("Installed: %1").arg(updater.installedVersion)
                  + (updater.lastCheck.length > 0
                       ? qsTr("   Last checked: %1").arg(updater.lastCheck)
                       : qsTr("   Not checked yet"))
                : ""
        }

        Note
        {
          width: parent.width - Math.round(36 * menu.u)
          x: Math.round(18 * menu.u)
          visible: updater && updater.checkMessage.length > 0
          text: updater ? updater.checkMessage : ""
          color: (updater && updater.checkStatus === menu.checkFailed)
                   ? menu.accentText : menu.textSec
        }

        Item
        {
          width: parent.width
          height: Math.round(96 * menu.u)

          Btn
          {
            id: checkButton
            anchors.left: parent.left
            anchors.leftMargin: Math.round(18 * menu.u)
            anchors.verticalCenter: parent.verticalCenter
            label: (updater && updater.checkStatus === menu.checkBusy)
                     ? qsTr("Checking…") : qsTr("Check for updates")
            hint: qsTr("Ask the release feed whether a newer version exists")
            minW: 330
            onClicked:
            {
              if (updater)
                updater.checkNow()
            }
          }

          Btn
          {
            id: installButton
            anchors.left: checkButton.right
            anchors.leftMargin: Math.round(16 * menu.u)
            anchors.verticalCenter: parent.verticalCenter
            visible: updater && updater.state === menu.stateAvailable
            label: updater && updater.availableVersion.length > 0
                     ? qsTr("Install %1").arg(updater.availableVersion)
                     : qsTr("Install update")
            hint: qsTr("Download and install the update, then restart Plex")
            primary: true
            minW: 300
            onClicked:
            {
              if (!updater)
                return;

              if (cfg)
                cfg.hide()

              updater.applyUpdate()
            }
          }

          Btn
          {
            id: notesButton
            anchors.left: installButton.visible ? installButton.right : checkButton.right
            anchors.leftMargin: Math.round(16 * menu.u)
            anchors.verticalCenter: parent.verticalCenter
            label: qsTr("Release notes")
            hint: qsTr("Show what changed in the latest release")
            minW: 280
            onClicked: menu.openReleaseNotes()
          }
        }

        Toggle
        {
          width: parent.width
          label: qsTr("Check for updates automatically")
          hint: qsTr("When this is off, nothing is checked unless you press Check for updates")
          checked: updater ? updater.checkEnabled : false
          onToggled: function(value)
          {
            if (updater)
              updater.setCheckEnabled(value)
          }
        }

        Picker
        {
          width: parent.width
          label: qsTr("How often to check")
          hint: qsTr("Checks happen in the background and never install anything on their own")
          options: [
            { value: "startup", label: qsTr("Every time Plex starts") },
            { value: "daily", label: qsTr("Once a day") },
            { value: "weekly", label: qsTr("Once a week") },
            { value: "manual", label: qsTr("Only when I ask") }
          ]
          current: updater ? updater.cadence : "startup"
          currentLabel: menu.cadenceLabel(updater ? updater.cadence : "startup")
          onPicked: function(value)
          {
            if (updater)
              updater.setCadence(value)
          }
        }

        Item
        {
          width: parent.width
          height: Math.round(96 * menu.u)

          Btn
          {
            anchors.left: parent.left
            anchors.leftMargin: Math.round(18 * menu.u)
            anchors.verticalCenter: parent.verticalCenter
            visible: updater && updater.hasSkipped
            label: qsTr("Stop skipping a version")
            hint: qsTr("Offer the version that was skipped again")
            minW: 330
            onClicked:
            {
              if (updater)
                updater.forgetSkipped()
            }
          }

          Btn
          {
            anchors.right: parent.right
            anchors.rightMargin: Math.round(18 * menu.u)
            anchors.verticalCenter: parent.verticalCenter
            label: qsTr("Restore update defaults")
            minW: 330
            onClicked:
            {
              if (cfg)
                cfg.restoreDefaults("updates")
            }
          }
        }

        Heading
        {
          width: parent.width
          text: qsTr("Downloads")
        }

        Picker
        {
          width: parent.width
          label: qsTr("Save downloads to")
          hint: cfg ? cfg.downloadDir : ""
          options: [
            { value: "downloads", label: qsTr("Downloads folder") },
            { value: "appdata", label: qsTr("Plex's own data folder") },
            { value: "custom", label: qsTr("A folder I choose") }
          ]
          current: cfg ? cfg.downloadPreset : "downloads"
          currentLabel: menu.presetLabel(cfg ? cfg.downloadPreset : "downloads")
          onPicked: function(value) { menu.applyPreset(value) }
        }

        Item
        {
          width: parent.width
          height: menu.customPathOpen ? Math.round(104 * menu.u) : 0
          visible: menu.customPathOpen

          Rectangle
          {
            id: pathBox
            anchors.left: parent.left
            anchors.leftMargin: Math.round(18 * menu.u)
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width * 0.58
            height: Math.round(64 * menu.u)
            radius: Math.round(8 * menu.u)
            color: menu.surface
            border.width: Math.max(1, Math.round(2 * menu.u))
            border.color: pathInput.activeFocus ? menu.accentFill : menu.outline

            TextInput
            {
              id: pathInput
              anchors.fill: parent
              anchors.leftMargin: Math.round(16 * menu.u)
              anchors.rightMargin: Math.round(16 * menu.u)
              verticalAlignment: TextInput.AlignVCenter
              text: menu.customPath
              color: menu.textPri
              font.pixelSize: menu.fontPx(26)
              selectByMouse: true
              activeFocusOnTab: true
              clip: true

              Accessible.role: Accessible.EditableText
              Accessible.name: qsTr("Download folder")

              onTextChanged: menu.customPath = text
              onAccepted: pathSave.clicked()

              Keys.onUpPressed: menu.step(pathInput, false)
              Keys.onDownPressed: menu.step(pathInput, true)

              onActiveFocusChanged:
              {
                if (activeFocus)
                  menu.reveal(pathBox)
              }
            }

            Text
            {
              anchors.fill: parent
              anchors.leftMargin: Math.round(16 * menu.u)
              verticalAlignment: Text.AlignVCenter
              visible: pathInput.text.length === 0
              text: qsTr("/home/you/Downloads")
              color: menu.textSec
              font.pixelSize: menu.fontPx(26)
            }
          }

          Btn
          {
            id: pathSave
            anchors.left: pathBox.right
            anchors.leftMargin: Math.round(16 * menu.u)
            anchors.verticalCenter: pathBox.verticalCenter
            label: qsTr("Use this folder")
            hint: qsTr("Check the folder can be written to and save it")
            primary: true
            minW: 300
            onClicked:
            {
              if (cfg)
                cfg.applyDownloadDir(menu.customPath)
            }
          }
        }

        Note
        {
          width: parent.width - Math.round(36 * menu.u)
          x: Math.round(18 * menu.u)
          text:
          {
            if (!cfg)
              return "";

            if (cfg.downloadStatus.length > 0)
              return cfg.downloadStatus;

            return cfg.downloadWritable
                     ? qsTr("%1 can be written to").arg(cfg.downloadDir)
                     : qsTr("%1 cannot be written to").arg(cfg.downloadDir);
          }
          color: (cfg && !cfg.downloadWritable) ? menu.accentText : menu.textSec
        }

        Toggle
        {
          width: parent.width
          label: qsTr("Ask before every download")
          hint: qsTr("Plex shows this menu with Save and Cancel instead of saving straight away")
          checked: cfg ? cfg.downloadAsk : false
          onToggled: function(value)
          {
            if (settings)
              settings.setValue("main", "downloadAsk", value)
          }
        }

        Item
        {
          width: parent.width
          height: Math.round(96 * menu.u)

          Btn
          {
            anchors.right: parent.right
            anchors.rightMargin: Math.round(18 * menu.u)
            anchors.verticalCenter: parent.verticalCenter
            label: qsTr("Restore download defaults")
            minW: 330
            onClicked:
            {
              if (cfg)
              {
                cfg.restoreDefaults("downloads")
                menu.customPathOpen = false
              }
            }
          }
        }

        Heading
        {
          width: parent.width
          text: qsTr("Keys and remotes")
        }

        Picker
        {
          width: parent.width
          label: qsTr("Key that opens this menu")
          hint: qsTr("On a controller, map the Menu or Start button to this key")
          options: [
            { value: "F1", label: "F1" },
            { value: "F2", label: "F2" },
            { value: "F3", label: "F3" },
            { value: "F4", label: "F4" },
            { value: "F8", label: "F8" },
            { value: "Menu", label: qsTr("Menu key") }
          ]
          current: cfg ? cfg.menuKey : "F1"
          currentLabel: cfg ? cfg.menuKey : "F1"
          onPicked: function(value)
          {
            if (settings)
              settings.setValue("main", "menuKey", value)
          }
        }

        Picker
        {
          width: parent.width
          label: qsTr("Gamepad button that opens this menu")
          hint: qsTr("Button numbers follow the controller's own order")
          options: [
            { value: "KEY_BUTTON_7", label: qsTr("Button 7 (Start)") },
            { value: "KEY_BUTTON_6", label: qsTr("Button 6 (Back)") },
            { value: "KEY_BUTTON_2", label: qsTr("Button 2 (X)") },
            { value: "KEY_BUTTON_3", label: qsTr("Button 3 (Y)") },
            { value: "KEY_BUTTON_8", label: qsTr("Button 8") },
            { value: "KEY_BUTTON_9", label: qsTr("Button 9") },
            { value: "KEY_BUTTON_10", label: qsTr("Button 10") },
            { value: "", label: qsTr("None") }
          ]
          current: cfg ? cfg.menuButton : "KEY_BUTTON_7"
          currentLabel: cfg && cfg.menuButton !== "" ? cfg.menuButton : qsTr("None")
          onPicked: function(value)
          {
            if (settings)
              settings.setValue("main", "menuButton", value)
          }
        }

        Note
        {
          width: parent.width - Math.round(36 * menu.u)
          x: Math.round(18 * menu.u)
          text: qsTr("These are the keys Plex already answers to. Remotes and CEC devices "
                     + "send the same actions.")
        }

        Repeater
        {
          model: cfg ? cfg.keyBindings : []

          Item
          {
            width: list.width
            height: Math.round(42 * menu.u)

            Accessible.role: Accessible.StaticText
            Accessible.name: qsTr("%1 does %2").arg(modelData.key).arg(modelData.action)

            Text
            {
              anchors.left: parent.left
              anchors.leftMargin: Math.round(18 * menu.u)
              anchors.verticalCenter: parent.verticalCenter
              width: parent.width * 0.3
              text: modelData.key
              color: menu.accentText
              font.pixelSize: menu.fontPx(24)
              font.weight: Font.DemiBold
              elide: Text.ElideRight
            }

            Text
            {
              anchors.left: parent.left
              anchors.leftMargin: parent.width * 0.32
              anchors.right: parent.right
              anchors.verticalCenter: parent.verticalCenter
              text: modelData.action
              color: menu.textSec
              font.pixelSize: menu.fontPx(24)
              elide: Text.ElideRight
            }
          }
        }

        Item
        {
          width: parent.width
          height: Math.round(96 * menu.u)

          Btn
          {
            anchors.right: parent.right
            anchors.rightMargin: Math.round(18 * menu.u)
            anchors.verticalCenter: parent.verticalCenter
            label: qsTr("Restore key defaults")
            minW: 330
            onClicked:
            {
              if (cfg)
                cfg.restoreDefaults("keys")
            }
          }
        }

        Heading
        {
          width: parent.width
          visible: menu.steam !== null && menu.steam.steamPresent
          text: qsTr("Steam")
        }

        Note
        {
          width: parent.width - Math.round(36 * menu.u)
          x: Math.round(18 * menu.u)
          visible: menu.steam !== null && menu.steam.steamPresent
          text: menu.steam && menu.steam.launchedFromSteam
                  ? qsTr("Steam started this session, so the device is running "
                         + "Plex on its game performance profile.")
                  : (menu.steam && menu.steam.shortcutPresent
                       ? qsTr("Steam has an icon for Plex, but this session was not "
                              + "started from it. Starting it from Steam gives the "
                              + "device the performance profile it reserves for "
                              + "games, which is what 4K playback needs.")
                       : qsTr("Steam has no icon for Plex. Adding one lets the "
                              + "device run Plex on the performance profile it "
                              + "reserves for games, which is what 4K playback "
                              + "needs."))
        }

        Note
        {
          width: parent.width - Math.round(36 * menu.u)
          x: Math.round(18 * menu.u)
          visible: menu.steamStatus.length > 0
          text: menu.steamStatus
        }

        Item
        {
          width: parent.width
          visible: menu.steam !== null && menu.steam.steamPresent
          height: visible ? Math.round(96 * menu.u) : 0

          Btn
          {
            id: steamShortcutButton
            anchors.left: parent.left
            anchors.leftMargin: Math.round(18 * menu.u)
            anchors.verticalCenter: parent.verticalCenter
            label: menu.steam && menu.steam.shortcutPresent
                     ? qsTr("Rewrite Steam icon")
                     : qsTr("Add Steam icon")
            hint: menu.steam && menu.steam.shortcutPresent
                    ? qsTr("Write the Plex entry again, for one that was changed "
                           + "or deleted")
                    : qsTr("Write a Plex entry into your Steam library")
            minW: 330
            onClicked: menu.createSteamShortcut()
          }

          Btn
          {
            anchors.left: steamShortcutButton.right
            anchors.leftMargin: Math.round(16 * menu.u)
            anchors.verticalCenter: parent.verticalCenter
            visible: menu.steam !== null && menu.steam.shouldOfferRelaunch
            label: qsTr("Restart through Steam")
            hint: qsTr("Close Plex and ask Steam to start it again")
            minW: 330
            onClicked: menu.relaunchViaSteam()
          }
        }

        Heading
        {
          width: parent.width
          visible: menu.xr !== null && menu.xr.supported
          text: qsTr("Stereoscopic 3D")
        }

        Note
        {
          width: parent.width - Math.round(36 * menu.u)
          x: Math.round(18 * menu.u)
          visible: menu.xr !== null && menu.xr.supported
          text: qsTr("Side-by-side and over-under files can be shown with real "
                     + "depth through the headset. The headset is driven by a "
                     + "helper outside the sandbox, which Plex starts only while "
                     + "this is on.")
        }

        Toggle
        {
          width: parent.width
          visible: menu.xr !== null && menu.xr.supported
          label: qsTr("Play 3D files through the headset")
          hint: qsTr("Off plays them flat, as a doubled image")
          checked: menu.xr !== null && menu.xr.enabled
          onToggled: function(value)
          {
            if (menu.xr)
              menu.xr.enabled = value
          }
        }

        Picker
        {
          width: parent.width
          visible: menu.xr !== null && menu.xr.supported && menu.xr.enabled
          label: qsTr("How 3D files are packed")
          hint: qsTr("Left as worked out, unless a file is wrong and needs saying")
          options: [
            { value: "auto", label: qsTr("Work it out per file") },
            { value: "sbs_half", label: qsTr("Side-by-side, half width") },
            { value: "sbs", label: qsTr("Side-by-side, full width") },
            { value: "ou_half", label: qsTr("Over-under, half height") },
            { value: "ou", label: qsTr("Over-under, full height") },
            { value: "mono", label: qsTr("Treat everything as 2D") }
          ]
          current: menu.xr ? menu.xr.formatOverride : "auto"
          currentLabel: menu.packingLabel(menu.xr ? menu.xr.formatOverride : "auto")
          onPicked: function(value)
          {
            if (menu.xr)
              menu.xr.formatOverride = value
          }
        }

        Note
        {
          width: parent.width - Math.round(36 * menu.u)
          x: Math.round(18 * menu.u)
          visible: menu.xr !== null && menu.xr.supported && menu.xr.enabled
          text: menu.packingStatusText()
        }

        Toggle
        {
          width: parent.width
          visible: menu.xr !== null && menu.xr.supported && menu.xr.enabled
          label: qsTr("Swap left and right eye")
          hint: qsTr("For a file whose eyes are the other way round")
          checked: menu.xr !== null && menu.xr.swapEyes
          onToggled: function(value)
          {
            if (menu.xr)
              menu.xr.swapEyes = value
          }
        }

        Note
        {
          width: parent.width - Math.round(36 * menu.u)
          x: Math.round(18 * menu.u)
          visible: menu.xr !== null && menu.xr.supported && menu.xr.enabled
          text: menu.xrStatusText()
        }

        Item
        {
          width: parent.width
          visible: menu.xr !== null && menu.xr.supported && menu.xr.enabled
          height: visible ? Math.round(96 * menu.u) : 0

          Btn
          {
            anchors.left: parent.left
            anchors.leftMargin: Math.round(18 * menu.u)
            anchors.verticalCenter: parent.verticalCenter
            enabled: menu.xr !== null && !menu.xr.busy
            label: menu.xr && menu.xr.busy
                     ? qsTr("Checking...")
                     : qsTr("Check the headset")
            hint: qsTr("Takes the headset over for a moment to prove both eyes "
                       + "can be drawn to")
            minW: 330
            onClicked:
            {
              if (menu.xr)
                menu.xr.testOutput()
            }
          }
        }

        Heading
        {
          width: parent.width
          text: qsTr("Display")
        }

        Picker
        {
          width: parent.width
          label: qsTr("Text size in this menu")
          hint: qsTr("Also applies to the update messages")
          options: [
            { value: "normal", label: qsTr("Normal") },
            { value: "large", label: qsTr("Large") },
            { value: "larger", label: qsTr("Larger") }
          ]
          current: cfg ? cfg.textSizeName : "normal"
          currentLabel: menu.textSizeLabel(cfg ? cfg.textSizeName : "normal")
          onPicked: function(value)
          {
            if (settings)
              settings.setValue("main", "menuTextScale", value)
          }
        }

        Toggle
        {
          width: parent.width
          label: qsTr("High contrast")
          hint: qsTr("Black background, white outlines and brighter highlights")
          checked: cfg ? cfg.highContrast : false
          onToggled: function(value)
          {
            if (settings)
              settings.setValue("main", "menuHighContrast", value)
          }
        }

        Item
        {
          width: parent.width
          height: Math.round(96 * menu.u)

          Btn
          {
            anchors.right: parent.right
            anchors.rightMargin: Math.round(18 * menu.u)
            anchors.verticalCenter: parent.verticalCenter
            label: qsTr("Restore display defaults")
            minW: 330
            onClicked:
            {
              if (cfg)
                cfg.restoreDefaults("display")
            }
          }
        }

        Item
        {
          width: parent.width
          height: Math.round(24 * menu.u)
        }
      }
    }

    Rectangle
    {
      id: scrollTrack

      readonly property real span: flick.contentHeight - flick.height
      readonly property real room: height - scrollHandle.height

      visible: span > 1
      width: Math.round(10 * menu.u)
      radius: width / 2
      color: menu.track
      border.color: menu.contrast ? menu.outline : "transparent"
      border.width: menu.contrast ? Math.max(1, Math.round(menu.u)) : 0

      anchors.right: parent.right
      anchors.rightMargin: Math.round(10 * menu.u)
      anchors.top: flick.top
      anchors.bottom: flick.bottom

      MouseArea
      {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        onClicked: function(mouse)
        {
          if (scrollTrack.span <= 0)
            return;

          var page = flick.height * 0.9
          flick.contentY = Math.max(0, Math.min(scrollTrack.span,
                                                mouse.y < scrollHandle.y
                                                  ? flick.contentY - page
                                                  : flick.contentY + page))
        }
      }

      Rectangle
      {
        id: scrollHandle

        width: parent.width
        radius: width / 2
        height: Math.max(Math.round(48 * menu.u),
                         scrollTrack.height * (flick.height / Math.max(1, flick.contentHeight)))
        color: scrollDrag.drag.active || scrollDrag.containsMouse ? menu.accentFill : menu.outline

        Binding
        {
          target: scrollHandle
          property: "y"
          when: !scrollDrag.drag.active
          value: scrollTrack.span > 0
                   ? Math.max(0, Math.min(scrollTrack.room,
                                          flick.contentY / scrollTrack.span * scrollTrack.room))
                   : 0
          restoreMode: Binding.RestoreNone
        }

        Behavior on color
        {
          ColorAnimation { duration: 120 }
        }
      }

      MouseArea
      {
        id: scrollDrag

        anchors.fill: scrollHandle
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor

        drag.target: scrollHandle
        drag.axis: Drag.YAxis
        drag.minimumY: 0
        drag.maximumY: scrollTrack.room

        onPositionChanged:
        {
          if (!drag.active || scrollTrack.room <= 0)
            return;

          flick.contentY = scrollHandle.y / scrollTrack.room * scrollTrack.span
        }
      }
    }

    Rectangle
    {
      id: footerRule
      anchors.left: parent.left
      anchors.right: parent.right
      anchors.bottom: footer.top
      height: Math.max(1, Math.round(2 * menu.u))
      color: menu.rule
    }

    Item
    {
      id: footer
      anchors.left: parent.left
      anchors.right: parent.right
      anchors.bottom: parent.bottom
      anchors.leftMargin: Math.round(36 * menu.u)
      anchors.rightMargin: Math.round(36 * menu.u)
      height: Math.round(108 * menu.u)

      Btn
      {
        id: resetAll
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        label: qsTr("Restore all defaults")
        hint: qsTr("Put every setting in this menu back to how it shipped")
        minW: 340
        onClicked:
        {
          if (!cfg)
            return;

          cfg.restoreDefaults("all")
          menu.customPathOpen = false
        }
      }

      Btn
      {
        id: closeButton
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        label: qsTr("Close")
        hint: qsTr("Close this menu and go back to Plex")
        primary: true
        minW: 240
        onClicked: menu.dismiss()
      }
    }
  }

  FocusScope
  {
    id: picker

    property var target: null

    anchors.fill: parent
    visible: false

    function openFor(item)
    {
      picker.target = item
      pickerList.model = item.options
      pickerList.currentIndex = item.indexOfCurrent()
      pickerTitle.text = item.label
      picker.visible = true
      Qt.callLater(function() { pickerList.forceActiveFocus() })
    }

    function close()
    {
      var item = picker.target
      picker.visible = false
      picker.target = null

      if (item)
        item.forceActiveFocus()
    }

    function choose(index)
    {
      var item = picker.target
      var options = item ? item.options : []

      if (item && index >= 0 && index < options.length)
      {
        var value = options[index].value
        picker.close()
        item.picked(value)
        return;
      }

      picker.close()
    }

    Keys.onEscapePressed: picker.close()
    Keys.onBackPressed: picker.close()

    Rectangle
    {
      anchors.fill: parent
      color: "black"
      opacity: 0.6

      MouseArea
      {
        anchors.fill: parent
        onClicked: picker.close()
      }
    }

    Card
    {
      anchors.centerIn: parent
      width: Math.min(Math.round(760 * menu.u), menu.width - 2 * menu.inset)
      height: Math.min(pickerColumn.implicitHeight + Math.round(48 * menu.u),
                       menu.height - 2 * menu.inset)

      Accessible.role: Accessible.Dialog
      Accessible.name: pickerTitle.text

      Column
      {
        id: pickerColumn
        anchors.fill: parent
        anchors.margins: Math.round(24 * menu.u)
        spacing: Math.round(12 * menu.u)

        Text
        {
          id: pickerTitle
          width: parent.width
          color: menu.textPri
          font.pixelSize: menu.fontPx(30)
          font.weight: Font.Bold
          wrapMode: Text.WordWrap
        }

        ListView
        {
          id: pickerList

          width: parent.width
          height: Math.min(contentHeight, menu.height * 0.6)
          clip: true
          focus: true
          keyNavigationEnabled: true
          currentIndex: 0

          Accessible.role: Accessible.List
          Accessible.name: pickerTitle.text

          Keys.onReturnPressed: picker.choose(pickerList.currentIndex)
          Keys.onEnterPressed: picker.choose(pickerList.currentIndex)
          Keys.onSpacePressed: picker.choose(pickerList.currentIndex)

          delegate: Item
          {
            width: pickerList.width
            height: Math.round(72 * menu.u)

            readonly property bool chosen: picker.target
                                           && picker.target.current === modelData.value

            Accessible.role: Accessible.ListItem
            Accessible.name: modelData.label
            Accessible.selected: index === pickerList.currentIndex

            Rectangle
            {
              anchors.fill: parent
              radius: Math.round(8 * menu.u)
              color: index === pickerList.currentIndex ? menu.track : "transparent"
              border.width: index === pickerList.currentIndex
                              ? Math.round(3 * menu.u) : 0
              border.color: menu.ringOuter
            }

            Text
            {
              anchors.left: parent.left
              anchors.leftMargin: Math.round(18 * menu.u)
              anchors.verticalCenter: parent.verticalCenter
              text: modelData.label
              color: menu.textPri
              font.pixelSize: menu.fontPx(28)
              font.weight: index === pickerList.currentIndex ? Font.DemiBold : Font.Normal
            }

            Text
            {
              anchors.right: parent.right
              anchors.rightMargin: Math.round(18 * menu.u)
              anchors.verticalCenter: parent.verticalCenter
              visible: chosen
              text: "✓"
              color: menu.accentText
              font.pixelSize: menu.fontPx(28)
            }

            MouseArea
            {
              anchors.fill: parent
              cursorShape: Qt.PointingHandCursor
              onClicked:
              {
                pickerList.currentIndex = index
                picker.choose(index)
              }
            }
          }
        }
      }
    }
  }
}
