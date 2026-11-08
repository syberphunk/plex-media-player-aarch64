import QtQuick
import Konvergo 1.0
import QtWebEngine
import QtWebChannel
import QtQuick.Window

KonvergoWindow
{
  id: mainWindow
  title: "Plex Media Player"
  objectName: "mainWindow"
  minimumHeight: windowMinSize.height
  minimumWidth: windowMinSize.width

  function runWebAction(action)
  {
    if (mainWindow.webDesktopMode)
      web.triggerWebAction(action)
  }

  function goToSignIn()
  {
    var current = web.url.toString()

    mainWindow.requestSignIn()
    web.url = Qt.binding(function() { return mainWindow.webUrl })

    if (current === mainWindow.webUrl)
      web.reload()
  }

  function goToWebClientRoute(fragment)
  {
    web.runJavaScript("window.location.hash = " + JSON.stringify(fragment))
  }

  Shortcut
  {
    enabled: mainWindow.webDesktopMode
    sequence: "F11"
    onActivated: mainWindow.toggleWebMode()
  }

  Shortcut
  {
    enabled: mainWindow.webDesktopMode
    sequence: "Shift+F11"
    onActivated: mainWindow.toggleFullscreen()
  }

  Shortcut
  {
    enabled: mainWindow.webDesktopMode
    sequences: [ StandardKey.Close ]
    onActivated: mainWindow.close()
  }

  Shortcut
  {
    enabled: mainWindow.webDesktopMode
    sequence: "Meta+Down"
    onActivated: mainWindow.minimizeWindow()
  }

  Shortcut
  {
    enabled: mainWindow.webDesktopMode
    sequences: [ StandardKey.Quit ]
    onActivated: mainWindow.close()
  }

  Shortcut
  {
    sequence: "Ctrl+Shift+D"
    enabled: mainWindow.webDesktopMode
    onActivated: mainWindow.toggleDebug()
  }

  Shortcut
  {
    sequences: [ StandardKey.Copy ]
    onActivated: runWebAction(WebEngineView.Copy)
    id: action_copy
  }

  Shortcut
  {
    sequences: [ StandardKey.Cut ]
    onActivated: runWebAction(WebEngineView.Cut)
    id: action_cut
  }

  Shortcut
  {
    sequences: [ StandardKey.Paste ]
    onActivated: runWebAction(WebEngineView.Paste)
    id: action_paste
  }

  Shortcut
  {
    sequences: [ StandardKey.SelectAll ]
    onActivated: runWebAction(WebEngineView.SelectAll)
    id: action_selectall
  }

  Shortcut
  {
    sequences: [ StandardKey.Undo ]
    onActivated: runWebAction(WebEngineView.Undo)
    id: action_undo
  }

  Shortcut
  {
    sequences: [ StandardKey.Redo ]
    onActivated: runWebAction(WebEngineView.Redo)
    id: action_redo
  }

  MpvVideo
  {
    id: video
    objectName: "video"
  }

  WebEngineProfilePrototype
  {
    id: webProfile
    storageName: "plexmediaplayer"
    httpCacheType: WebEngineProfile.DiskHttpCache
    persistentCookiesPolicy: WebEngineProfile.ForcePersistentCookies
  }

  WebEngineView
  {
    id: web
    objectName: "web"
    settings.errorPageEnabled: false
    settings.localContentCanAccessRemoteUrls: true
    focus: true
    enabled: !menuOverlay.open && !updateOverlay.captures
    property string currentHoveredUrl: ""
    onLinkHovered: (hoveredUrl) => { web.currentHoveredUrl = hoveredUrl }
    onUrlChanged: mainWindow.webClientNavigated(web.url)
    width: mainWindow.width
    height: mainWindow.height

    Component.onCompleted:
    {
      web.profile = webProfile.instance()
      web.profile.httpUserAgent = components.system.getUserAgent()
      web.profile.downloadPath = components.menu.downloadDir

      var bridge = WebEngine.script()
      bridge.name = "qwebchannel"
      bridge.sourceUrl = "qrc:///qtwebchannel/qwebchannel.js"
      bridge.injectionPoint = WebEngineScript.DocumentCreation
      bridge.worldId = WebEngineScript.MainWorld
      web.userScripts.insert(bridge)

      var clientConfig = WebEngine.script()
      clientConfig.name = "plexweb"
      clientConfig.sourceCode = "(function() {" +
                                "  var override = { allowLoopbackDiscovery: false };" +
                                "  var merged = Object.assign({}, window.PLEXWEB, override);" +
                                "  Object.defineProperty(window, 'PLEXWEB', {" +
                                "    configurable: true, enumerable: true," +
                                "    get: function() { return merged; }," +
                                "    set: function(value) { merged = Object.assign({}, value, override); }" +
                                "  });" +
                                "})();"
      clientConfig.injectionPoint = WebEngineScript.DocumentCreation
      clientConfig.worldId = WebEngineScript.MainWorld
      web.userScripts.insert(clientConfig)

      web.url = Qt.binding(function() { return mainWindow.webUrl })

      forceActiveFocus()
      mainWindow.reloadWebClient.connect(reload)
    }

    onLoadingChanged: (loadRequest) =>
    {
      // we use a timer here to switch to the webview since
      // it take a few moments for the webview to render
      // after it has loaded.
      //
      if (loadRequest.status == WebEngineLoadingInfo.LoadStartedStatus)
      {
        console.log("WebEngineLoadRequest starting: " + loadRequest.url);
      }
      else if (loadRequest.status == WebEngineLoadingInfo.LoadSucceededStatus)
      {
        console.log("WebEngineLoadRequest success: " + loadRequest.url);

        if (!mainWindow.webDesktopMode)
          components.system.setCursorVisibility(false)
      }
      else if (loadRequest.status == WebEngineLoadingInfo.LoadFailedStatus)
      {
        console.log("WebEngineLoadRequest failure: " + loadRequest.url + " error code: " + loadRequest.errorCode);
        errorLabel.visible = true
        errorLabel.text = "Error loading client, this is bad and should not happen<br>" +
                          "You can try to <a href='reload'>reload</a> or head to our <a href='http://plex.tv/support'>support page</a><br><br>Actual Error: <pre>" +
                          loadRequest.errorString + " [" + loadRequest.errorCode + "]</pre><br><br>" +
                          "Provide the <a target='_blank' href='file://"+ components.system.logFilePath + "'>logfile</a> as well."
      }
    }

    onNewWindowRequested: (request) =>
    {
      if (request.userInitiated)
      {
        var target = request.requestedUrl.toString()
        if (target.length == 0)
          target = web.currentHoveredUrl
        console.log("Opening external URL: " + target)
        components.system.openExternalUrl(target)
      }
    }

    onFullScreenRequested: (request) =>
    {
      console.log("Request fullscreen: " + request.toggleOn)
      mainWindow.setFullScreen(request.toggleOn)
      request.accept()
    }

    onJavaScriptConsoleMessage: (level, message, lineNumber, sourceID) =>
    {
      components.system.info(message)
    }

    onCertificateError: (error) =>
    {
      console.log(error.url + " :" + error.description + error.type)
      error.rejectCertificate()
    }
  }

  Text
  {
    id: errorLabel
    z: 5
    anchors.centerIn: parent
    color: "#999999"
    linkColor: "#cc7b19"
    text: "Generic error"
    font.pixelSize: 32
    font.bold: true
    visible: false
    verticalAlignment: Text.AlignVCenter
    textFormat: Text.StyledText
    onLinkActivated: (link) =>
    {
      if (link == "reload")
      {
        errorLabel.visible = false
        web.reload()
      }
      else
      {
        Qt.openUrlExternally(link)
      }
    }
  }

  UpdateOverlay
  {
    id: updateOverlay
    z: 8
    anchors.fill: parent
    onClosed: Qt.callLater(web.forceActiveFocus)
  }

  MenuOverlay
  {
    id: menuOverlay
    z: 9
    anchors.fill: parent
    onClosed: Qt.callLater(web.forceActiveFocus)
    onSignIn: mainWindow.goToSignIn()
    onOnlineMediaSources: mainWindow.goToWebClientRoute("#!/settings/online-media-sources")
  }

  Rectangle
  {
    id: menuHandle
    z: 7

    readonly property real u: Math.max(0.6, Math.min(2.0, mainWindow.height / 1080))
    readonly property bool lit: handleArea.containsMouse

    readonly property real edge: Math.round(14 * u)

    x: parked ? parent.width - width - edge : edge
    y: parked ? Math.round((parent.height - height) / 2)
              : parent.height - height - edge

    width: Math.round(44 * u)
    height: width
    radius: width / 2
    color: lit ? "#E08B20" : "#22252B"
    border.color: lit ? "#E08B20" : "#767E8A"
    border.width: Math.max(1, Math.round(1.5 * u))

    readonly property bool parked: components.player.videoWindowVisible
                                   && components.xr !== null
                                   && components.xr.detectedStereo
    readonly property bool overFlatVideo: components.player.videoWindowVisible && !parked

    visible: opacity > 0
    opacity: (components.system.cursorVisible && !menuOverlay.open && !updateOverlay.captures && !overFlatVideo) ? 1 : 0

    Accessible.role: Accessible.Button
    Accessible.name: "Player settings"

    Behavior on opacity
    {
      NumberAnimation { duration: 120 }
    }

    Item
    {
      id: cog
      anchors.centerIn: parent
      width: Math.round(menuHandle.width * 0.52)
      height: width

      readonly property string ink: menuHandle.lit ? "#101216" : "#ECEEF1"
      readonly property real tooth: Math.max(2, Math.round(cog.width * 0.17))

      Repeater
      {
        model: 4

        Rectangle
        {
          anchors.centerIn: parent
          width: cog.tooth
          height: cog.width * 1.1
          radius: width / 2
          color: cog.ink
          rotation: index * 45
          antialiasing: true
        }
      }

      Rectangle
      {
        anchors.centerIn: parent
        width: cog.width * 0.78
        height: width
        radius: width / 2
        color: cog.ink
        antialiasing: true
      }

      Rectangle
      {
        anchors.centerIn: parent
        width: cog.width * 0.3
        height: width
        radius: width / 2
        color: menuHandle.color
        antialiasing: true
      }
    }

    MouseArea
    {
      id: handleArea
      anchors.fill: parent
      hoverEnabled: true
      cursorShape: Qt.PointingHandCursor
      onClicked: components.menu.show()
    }
  }

  Connections
  {
    target: web.profile
    ignoreUnknownSignals: true

    function onDownloadRequested(download)
    {
      var menu = components.menu
      var name = download.downloadFileName

      download.downloadDirectory = menu.downloadDir

      download.stateChanged.connect(function(state)
      {
        if (state === WebEngineDownloadRequest.DownloadCompleted)
          menu.noteDownload(name, "completed")
        else if (state === WebEngineDownloadRequest.DownloadCancelled)
          menu.noteDownload(name, "cancelled")
        else if (state === WebEngineDownloadRequest.DownloadInterrupted)
          menu.noteDownload(name, "failed")
      })

      if (menu.downloadAsk)
      {
        menuOverlay.askDownload(download)
        return
      }

      download.accept()
      menu.noteDownload(name, "started")
    }
  }

  Rectangle
  {
    id: debug
    color: "black"
    z: 10
    anchors.centerIn: parent
    width: parent.width
    height: parent.height
    opacity: 0.7
    visible: mainWindow.showDebugLayer

    Text
    {
      id: debugLabel
      width: (parent.width - 50) / 2
      height: parent.height - 25
      anchors.left: parent.left
      anchors.leftMargin: 64
      anchors.top: parent.top
      anchors.topMargin: 54
      anchors.bottomMargin: 54
      color: "white"
      font.pixelSize: Math.round(height / 65)
      wrapMode: Text.WrapAnywhere

      function windowDebug()
      {
        var dbg = mainWindow.debugInfo + "Window and web\n";
        dbg += "  Window size: " + parent.width + "x" + parent.height + " - " + web.width + "x" + web.height + "\n";
        dbg += "  DevicePixel ratio: " + Screen.devicePixelRatio + "\n";

        return dbg;
      }

      text: windowDebug()
    }

    Text
    {
      id: videoLabel
      width: (parent.width - 50) / 2
      height: parent.height - 25
      anchors.right: parent.right
      anchors.left: debugLabel.right
      anchors.rightMargin: 64
      anchors.top: parent.top
      anchors.topMargin: 54
      anchors.bottomMargin: 54
      color: "white"
      font.pixelSize: Math.round(height / 65)
      wrapMode: Text.WrapAnywhere

      text: mainWindow.videoInfo
    }
  }

  property QtObject webChannel: web.webChannel
}
