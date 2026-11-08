import QtQuick

FocusScope
{
  id: overlay

  property var capability: (typeof components !== "undefined") ? components.capability : null
  property var player: (typeof components !== "undefined") ? components.player : null

  readonly property bool playing: player ? player.videoWindowVisible : false
  readonly property bool below: capability ? capability.belowMinimum : false
  readonly property bool hidden: capability ? capability.dismissed : true

  readonly property var lines: capability ? capability.warnings : []
  readonly property var fixes: capability ? capability.remedies : []

  readonly property bool showBanner: playing && below && !hidden && lines.length > 0

  property real u: Math.max(0.6, Math.min(2.0, overlay.height / 1080))
  property real inset: Math.round(overlay.height * 0.035)

  readonly property string surface:    "#181A1F"
  readonly property string textPri:    "#ECEEF1"
  readonly property string textSec:    "#B9BFC8"
  readonly property string accentText: "#E79A3C"
  readonly property string accentFill: "#E08B20"
  readonly property string onAccent:   "#101216"
  readonly property string outline:    "#767E8A"
  readonly property string ringOuter:  "#F2F4F7"
  readonly property string ringInner:  "#0B0D10"

  signal closed()

  visible: showBanner
  enabled: showBanner

  function dismiss()
  {
    if (capability)
      capability.dismissed = true
    overlay.closed()
  }

  Timer
  {
    id: linger
    interval: 20000
    onTriggered: overlay.dismiss()
  }

  onShowBannerChanged:
  {
    if (showBanner)
      linger.restart()
    else
      linger.stop()
  }

  Rectangle
  {
    id: banner
    visible: overlay.showBanner

    radius: Math.round(14 * overlay.u)
    color: overlay.surface
    border.width: Math.max(1, Math.round(2 * overlay.u))
    border.color: overlay.accentFill

    width: Math.round(760 * overlay.u)
    height: heading.height + body.height + fix.height + close.height
            + Math.round(112 * overlay.u)

    anchors.horizontalCenter: parent.horizontalCenter
    anchors.top: parent.top
    anchors.topMargin: overlay.inset

    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("This device is below the tested minimum")

    Text
    {
      id: heading
      x: Math.round(32 * overlay.u)
      y: Math.round(26 * overlay.u)
      width: banner.width - Math.round(64 * overlay.u)
      wrapMode: Text.WordWrap
      textFormat: Text.PlainText
      text: qsTr("This device is below the tested minimum")
      color: overlay.textPri
      font.pixelSize: Math.round(30 * overlay.u)
      font.weight: Font.DemiBold
    }

    Column
    {
      id: body
      anchors.left: heading.left
      anchors.top: heading.bottom
      anchors.topMargin: Math.round(12 * overlay.u)
      width: heading.width
      spacing: Math.round(6 * overlay.u)

      Repeater
      {
        model: overlay.lines
        delegate: Text
        {
          width: body.width
          wrapMode: Text.WordWrap
          textFormat: Text.PlainText
          text: modelData
          color: overlay.textSec
          font.pixelSize: Math.round(24 * overlay.u)
        }
      }
    }

    Column
    {
      id: fix
      anchors.left: heading.left
      anchors.top: body.bottom
      anchors.topMargin: overlay.fixes.length > 0 ? Math.round(14 * overlay.u) : 0
      width: heading.width
      spacing: Math.round(6 * overlay.u)

      Repeater
      {
        model: overlay.fixes
        delegate: Text
        {
          width: fix.width
          wrapMode: Text.WordWrap
          textFormat: Text.PlainText
          text: modelData
          color: overlay.accentText
          font.pixelSize: Math.round(24 * overlay.u)
        }
      }
    }

    Rectangle
    {
      id: close
      anchors.right: parent.right
      anchors.rightMargin: Math.round(32 * overlay.u)
      anchors.top: fix.bottom
      anchors.topMargin: Math.round(20 * overlay.u)

      width: Math.round(200 * overlay.u)
      height: Math.round(56 * overlay.u)
      radius: Math.round(8 * overlay.u)
      color: overlay.accentFill

      activeFocusOnTab: true

      Accessible.role: Accessible.Button
      Accessible.name: qsTr("Continue")
      Accessible.onPressAction: overlay.dismiss()

      Keys.onReturnPressed: overlay.dismiss()
      Keys.onEnterPressed: overlay.dismiss()
      Keys.onSpacePressed: overlay.dismiss()

      Text
      {
        anchors.centerIn: parent
        text: qsTr("Continue")
        color: overlay.onAccent
        font.pixelSize: Math.round(26 * overlay.u)
        font.weight: Font.DemiBold
      }

      Rectangle
      {
        visible: close.activeFocus
        anchors.fill: parent
        anchors.margins: -Math.round(8 * overlay.u)
        radius: Math.round(16 * overlay.u)
        color: "transparent"
        border.width: Math.round(4 * overlay.u)
        border.color: overlay.ringOuter
      }

      Rectangle
      {
        visible: close.activeFocus
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
        onClicked: overlay.dismiss()
      }
    }
  }
}
